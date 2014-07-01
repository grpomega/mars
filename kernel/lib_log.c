/*  (c) 2010 Thomas Schoebel-Theuer / 1&1 Internet AG */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bio.h>

//#define BRICK_DEBUGGING
//#define MARS_DEBUGGING

#include "lib_log.h"

atomic_t global_aio_flying = ATOMIC_INIT(0);
EXPORT_SYMBOL_GPL(global_aio_flying);

void exit_logst(struct log_status *logst)
{
	int count;

	log_flush(logst);

	/*  TODO: replace by event */
	count = 0;
	while (atomic_read(&logst->aio_flying) > 0) {
		if (!count++)
			MARS_DBG("waiting for IO terminating...");
		brick_msleep(500);
	}
	if (logst->read_aio) {
		MARS_DBG("putting read_aio\n");
		GENERIC_INPUT_CALL(logst->input, aio_put, logst->read_aio);
		logst->read_aio = NULL;
	}
	if (logst->log_aio) {
		MARS_DBG("putting log_aio\n");
		GENERIC_INPUT_CALL(logst->input, aio_put, logst->log_aio);
		logst->log_aio = NULL;
	}
}
EXPORT_SYMBOL_GPL(exit_logst);

void init_logst(struct log_status *logst, struct mars_input *input, loff_t start_pos, loff_t end_pos)
{
	exit_logst(logst);

	memset(logst, 0, sizeof(struct log_status));

	logst->input = input;
	logst->brick = input->brick;
	logst->start_pos = start_pos;
	logst->log_pos = start_pos;
	logst->end_pos = end_pos;
	init_waitqueue_head(&logst->event);
}
EXPORT_SYMBOL_GPL(init_logst);

#define MARS_LOG_CB_MAX			32

struct log_cb_info {
	struct aio_object *aio;
	struct log_status *logst;
	struct semaphore mutex;
	atomic_t refcount;
	int nr_cb;

	/* make checkpatch.pl happy with a blank line - is this a false positive? */

	void (*endios[MARS_LOG_CB_MAX])(void *private, int error);
	void *privates[MARS_LOG_CB_MAX];
};

static
void put_log_cb_info(struct log_cb_info *cb_info)
{
	if (atomic_dec_and_test(&cb_info->refcount))
		brick_mem_free(cb_info);
}

static
void _do_callbacks(struct log_cb_info *cb_info, int error)
{
	int i;

	down(&cb_info->mutex);
	for (i = 0; i < cb_info->nr_cb; i++) {
		void (*end_fn)(void *private, int error);

		end_fn = cb_info->endios[i];
		cb_info->endios[i] = NULL;
		if (end_fn)
			end_fn(cb_info->privates[i], error);
	}
	up(&cb_info->mutex);
}

static
void log_write_endio(struct generic_callback *cb)
{
	struct log_cb_info *cb_info = cb->cb_private;
	struct log_status *logst;

	LAST_CALLBACK(cb);
	CHECK_PTR(cb_info, err);

	logst = cb_info->logst;
	CHECK_PTR(logst, done);

	_do_callbacks(cb_info, cb->cb_error);

done:
	put_log_cb_info(cb_info);
	atomic_dec(&logst->aio_flying);
	atomic_dec(&global_aio_flying);
	if (logst->signal_event)
		wake_up_interruptible(logst->signal_event);

	goto out_return;
err:
	MARS_FAT("internal pointer corruption\n");
out_return:;
}

void log_flush(struct log_status *logst)
{
	struct aio_object *aio = logst->log_aio;
	struct log_cb_info *cb_info;
	int align_size;
	int gap;

	if (!aio || !logst->count)
		goto out_return;
	gap = 0;
	align_size = (logst->align_size / PAGE_SIZE) * PAGE_SIZE;
	if (align_size > 0) {
		/*  round up to next alignment border */
		int align_offset = logst->offset & (align_size-1);

		if (align_offset > 0) {
			int restlen = aio->io_len - logst->offset;

			gap = align_size - align_offset;
			if (unlikely(gap > restlen))
				gap = restlen;
		}
	}
	if (gap > 0) {
		/*  don't leak information from kernelspace */
		memset(aio->io_data + logst->offset, 0, gap);
		logst->offset += gap;
	}
	aio->io_len = logst->offset;
	memcpy(&logst->log_pos_stamp, &logst->tmp_pos_stamp, sizeof(logst->log_pos_stamp));

	cb_info = logst->private;
	logst->private = NULL;
	SETUP_CALLBACK(aio, log_write_endio, cb_info);
	cb_info->logst = logst;
	aio->io_rw = 1;

	atomic_inc(&logst->aio_flying);
	atomic_inc(&global_aio_flying);

	GENERIC_INPUT_CALL(logst->input, aio_io, aio);
	GENERIC_INPUT_CALL(logst->input, aio_put, aio);

	logst->log_pos += logst->offset;
	logst->offset = 0;
	logst->count = 0;
	logst->log_aio = NULL;

	put_log_cb_info(cb_info);
out_return:;
}
EXPORT_SYMBOL_GPL(log_flush);

void *log_reserve(struct log_status *logst, struct log_header *lh)
{
	struct log_cb_info *cb_info = logst->private;
	struct aio_object *aio;
	void *data;

	short total_len = lh->l_len + OVERHEAD;
	int offset;
	int status;

	if (unlikely(lh->l_len <= 0 || lh->l_len > logst->max_size)) {
		MARS_ERR("trying to write %d bytes, max allowed = %d\n", lh->l_len, logst->max_size);
		goto err;
	}

	aio = logst->log_aio;
	if ((aio && total_len > aio->io_len - logst->offset)
	   || !cb_info || cb_info->nr_cb >= MARS_LOG_CB_MAX) {
		log_flush(logst);
	}

	aio = logst->log_aio;
	if (!aio) {
		if (unlikely(logst->private)) {
			MARS_ERR("oops\n");
			brick_mem_free(logst->private);
		}
		logst->private = brick_zmem_alloc(sizeof(struct log_cb_info));
		cb_info = logst->private;
		sema_init(&cb_info->mutex, 1);
		atomic_set(&cb_info->refcount, 2);

		aio = mars_alloc_aio(logst->brick);
		cb_info->aio = aio;

		aio->io_pos = logst->log_pos;
		aio->io_len = logst->chunk_size ? logst->chunk_size : total_len;
		aio->io_may_write = WRITE;
		aio->io_prio = logst->io_prio;

		for (;;) {
			status = GENERIC_INPUT_CALL(logst->input, aio_get, aio);
			if (likely(status >= 0))
				break;
			if (status != -ENOMEM && status != -EAGAIN) {
				MARS_ERR("aio_get() failed, status = %d\n", status);
				goto err_free;
			}
			brick_msleep(100);
		}

		if (unlikely(aio->io_len < total_len)) {
			MARS_ERR("io_len = %d total_len = %d\n", aio->io_len, total_len);
			goto put;
		}

		logst->offset = 0;
		logst->log_aio = aio;
	}

	offset = logst->offset;
	data = aio->io_data;
	DATA_PUT(data, offset, START_MAGIC);
	DATA_PUT(data, offset, (char)FORMAT_VERSION);
	logst->validflag_offset = offset;
	DATA_PUT(data, offset, (char)0); /*  valid_flag */
	DATA_PUT(data, offset, total_len); /*  start of next header */
	DATA_PUT(data, offset, lh->l_stamp.tv_sec);
	DATA_PUT(data, offset, lh->l_stamp.tv_nsec);
	DATA_PUT(data, offset, lh->l_pos);
	logst->reallen_offset = offset;
	DATA_PUT(data, offset, lh->l_len);
	DATA_PUT(data, offset, (short)0); /*  spare */
	DATA_PUT(data, offset, (int)0); /*  spare */
	DATA_PUT(data, offset, lh->l_code);
	DATA_PUT(data, offset, (short)0); /*  spare */

	/*  remember the last timestamp */
	memcpy(&logst->tmp_pos_stamp, &lh->l_stamp, sizeof(logst->tmp_pos_stamp));

	logst->payload_offset = offset;
	logst->payload_len = lh->l_len;

	return data + offset;

put:
	GENERIC_INPUT_CALL(logst->input, aio_put, aio);
	logst->log_aio = NULL;
	return NULL;

err_free:
	obj_free(aio);
	if (logst->private) {
		/*  TODO: if callbacks are already registered, call them here with some error code */
		brick_mem_free(logst->private);
		logst->private = NULL;
	}
err:
	return NULL;
}
EXPORT_SYMBOL_GPL(log_reserve);

bool log_finalize(struct log_status *logst, int len, void (*endio)(void *private, int error), void *private)
{
	struct aio_object *aio = logst->log_aio;
	struct log_cb_info *cb_info = logst->private;
	struct timespec now;
	void *data;
	int offset;
	int restlen;
	int nr_cb;
	int crc;
	bool ok = false;

	CHECK_PTR(aio, err);

	if (unlikely(len > logst->payload_len)) {
		MARS_ERR("trying to write more than reserved (%d > %d)\n", len, logst->payload_len);
		goto err;
	}
	restlen = aio->io_len - logst->offset;
	if (unlikely(len + END_OVERHEAD > restlen)) {
		MARS_ERR("trying to write more than available (%d > %d)\n", len, (int)(restlen - END_OVERHEAD));
		goto err;
	}
	if (unlikely(!cb_info || cb_info->nr_cb >= MARS_LOG_CB_MAX)) {
		MARS_ERR("too many endio() calls\n");
		goto err;
	}

	data = aio->io_data;

	crc = 0;
	if (logst->do_crc) {
		unsigned char checksum[mars_digest_size];

		mars_digest(checksum, data + logst->payload_offset, len);
		crc = *(int *)checksum;
	}

	/* Correct the length in the header.
	 */
	offset = logst->reallen_offset;
	DATA_PUT(data, offset, (short)len);

	/* Write the trailer.
	 */
	offset = logst->payload_offset + len;
	DATA_PUT(data, offset, END_MAGIC);
	DATA_PUT(data, offset, crc);
	DATA_PUT(data, offset, (char)1);  /*  valid_flag copy */
	DATA_PUT(data, offset, (char)0);  /*  spare */
	DATA_PUT(data, offset, (short)0); /*  spare */
	DATA_PUT(data, offset, logst->seq_nr + 1);
	get_lamport(&now);    /*  when the log entry was ready. */
	DATA_PUT(data, offset, now.tv_sec);
	DATA_PUT(data, offset, now.tv_nsec);

	if (unlikely(offset > aio->io_len)) {
		MARS_FAT("length calculation was wrong: %d > %d\n", offset, aio->io_len);
		goto err;
	}
	logst->offset = offset;

	/* This must come last. In case of incomplete
	 * or even overlapping disk transfers, this indicates
	 * the completeness / integrity of the payload at
	 * the time of starting the transfer.
	 */
	offset = logst->validflag_offset;
	DATA_PUT(data, offset, (char)1);

	nr_cb = cb_info->nr_cb++;
	cb_info->endios[nr_cb] = endio;
	cb_info->privates[nr_cb] = private;

	/*  report success */
	logst->seq_nr++;
	logst->count++;
	ok = true;

err:
	return ok;
}
EXPORT_SYMBOL_GPL(log_finalize);

static
void log_read_endio(struct generic_callback *cb)
{
	struct log_status *logst = cb->cb_private;

	LAST_CALLBACK(cb);
	CHECK_PTR(logst, err);
	logst->error_code = cb->cb_error;
	logst->got = true;
	wake_up_interruptible(&logst->event);
	goto out_return;
err:
	MARS_FAT("internal pointer corruption\n");
out_return:;
}

int log_read(struct log_status *logst, bool sloppy, struct log_header *lh, void **payload, int *payload_len)
{
	struct aio_object *aio;
	int old_offset;
	int status;

restart:
	status = 0;
	aio = logst->read_aio;
	if (!aio || logst->do_free) {
		loff_t this_len;

		if (aio) {
			GENERIC_INPUT_CALL(logst->input, aio_put, aio);
			logst->read_aio = NULL;
			logst->log_pos += logst->offset;
			logst->offset = 0;
		}

		this_len = logst->end_pos - logst->log_pos;
		if (this_len > logst->chunk_size) {
			this_len = logst->chunk_size;
		} else if (unlikely(this_len <= 0)) {
			MARS_ERR("tried bad IO len %lld, start_pos = %lld log_pos = %lld end_pos = %lld\n",
				this_len,
				logst->start_pos,
				logst->log_pos,
				logst->end_pos);
			status = -EOVERFLOW;
			goto done;
		}

		aio = mars_alloc_aio(logst->brick);
		aio->io_pos = logst->log_pos;
		aio->io_len = this_len;
		aio->io_prio = logst->io_prio;

		status = GENERIC_INPUT_CALL(logst->input, aio_get, aio);
		if (unlikely(status < 0)) {
			if (status != -ENODATA)
				MARS_ERR("aio_get() failed, status = %d\n", status);
			goto done_free;
		}
		if (unlikely(aio->io_len <= OVERHEAD)) { /*  EOF */
			status = 0;
			goto done_put;
		}

		SETUP_CALLBACK(aio, log_read_endio, logst);
		aio->io_rw = READ;
		logst->offset = 0;
		logst->got = false;
		logst->do_free = false;

		GENERIC_INPUT_CALL(logst->input, aio_io, aio);

		wait_event_interruptible_timeout(logst->event, logst->got, 60 * HZ);
		status = -EIO;
		if (!logst->got)
			goto done_put;
		status = logst->error_code;
		if (status < 0)
			goto done_put;
		logst->read_aio = aio;
	}

	status = log_scan(aio->io_data + logst->offset,
			  aio->io_len - logst->offset,
			  aio->io_pos,
			  logst->offset,
			  sloppy,
			  lh,
			  payload,
			  payload_len,
			  &logst->seq_nr);

	if (unlikely(status == 0)) {
		MARS_ERR("bad logfile scan\n");
		status = -EINVAL;
	}
	if (unlikely(status < 0))
		goto done_put;

	/*  memoize success */
	logst->offset += status;
	if (logst->offset + (logst->max_size + OVERHEAD) * 2 >= aio->io_len)
		logst->do_free = true;

done:
	if (status == -ENODATA) {
		/*  indicate EOF */
		status = 0;
	}
	return status;

done_put:
	old_offset = logst->offset;
	if (aio) {
		GENERIC_INPUT_CALL(logst->input, aio_put, aio);
		logst->read_aio = NULL;
		logst->log_pos += logst->offset;
		logst->offset = 0;
	}
	if (status == -EAGAIN && old_offset > 0)
		goto restart;
	goto done;

done_free:
	obj_free(aio);
	logst->read_aio = NULL;
	goto done;

}
EXPORT_SYMBOL_GPL(log_read);

/***************** module init stuff ************************/

int __init init_log_format(void)
{
	MARS_INF("init_log_format()\n");
	return 0;
}

void exit_log_format(void)
{
	MARS_INF("exit_log_format()\n");
}
