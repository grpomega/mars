// (c) 2010 Thomas Schoebel-Theuer / 1&1 Internet AG

/* Usebuf brick.
 * translates from unbuffered IO to buffered IO (mars_{get,put}_buf)
 */

//#define BRICK_DEBUGGING
//#define MARS_DEBUGGING

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/bio.h>

#include "mars.h"

///////////////////////// own type definitions ////////////////////////

#include "mars_usebuf.h"

///////////////////////// own helper functions ////////////////////////

/* currently we have copy semantics :(
 */
static void _usebuf_copy(struct usebuf_mars_ref_aspect *mref_a, int rw)
{
	void *ref_data = mref_a->object->ref_data;
	void *bio_base = kmap_atomic(mref_a->bvec->bv_page, KM_USER0);
	void *bio_data = bio_base + mref_a->bvec_offset;
	int len = mref_a->bvec_len;
#if 1
	if (rw == READ) {
		memcpy(bio_data, ref_data, len);
	} else {
		memcpy(ref_data, bio_data, len);
	}
#endif
	kunmap_atomic(bio_base, KM_USER0);
}

static void usebuf_ref_put(struct usebuf_output *output, struct mars_ref_object *origmref);

static void _usebuf_origmref_endio(struct usebuf_output *output, struct mars_ref_object *origmref)
{
	struct usebuf_mars_ref_aspect *origmref_a;
	struct generic_callback *cb;

	origmref_a = usebuf_mars_ref_get_aspect(output, origmref);
	if (unlikely(!origmref_a)) {
		MARS_FAT("cannot get origmref_a from origmref %p\n", origmref);
		goto out;
	}

	MARS_DBG("origmref=%p subref_count=%d error=%d\n", origmref, atomic_read(&origmref_a->subref_count), origmref->cb_error);

	CHECK_ATOMIC(&origmref_a->subref_count, 1);
	if (!atomic_dec_and_test(&origmref_a->subref_count)) {
		goto out;
	}

	cb = origmref->ref_cb;
	MARS_DBG("DONE error=%d\n", cb->cb_error);
	cb->cb_fn(cb);
	usebuf_ref_put(output, origmref);

out:
	return;
}

static void _usebuf_mref_endio(struct generic_callback *cb)
{
	struct usebuf_mars_ref_aspect *mref_a;
	struct mars_ref_object *mref;
	struct usebuf_output *output;
	struct mars_ref_object *origmref;
	struct usebuf_mars_ref_aspect *origmref_a;
	int status;

	mref_a = cb->cb_private;
	if (unlikely(!mref_a)) {
		MARS_FAT("cannot get aspect\n");
		goto out_fatal;
	}
	mref = mref_a->object;
	if (unlikely(!mref)) {
		MARS_FAT("cannot get mref\n");
		goto out_fatal;
	}
	output = mref_a->output;
	if (unlikely(!output)) {
		MARS_FAT("bad argument output\n");
		goto out_fatal;
	}
	origmref = mref_a->origmref;
	if (unlikely(!origmref)) {
		MARS_FAT("cannot get origmref\n");
		goto out_fatal;
	}
	MARS_DBG("origmref=%p\n", origmref);
	status = -EINVAL;
	origmref_a = usebuf_mars_ref_get_aspect(output, origmref);
	if (unlikely(!origmref_a)) {
		MARS_ERR("cannot get origmref_a\n");
		goto out_err;
	}

	// check if we have an initial read => now start the final write
	if (mref->ref_may_write != READ && mref->ref_rw == READ && cb->cb_error >= 0) {
		struct usebuf_input *input = output->brick->inputs[0];

		status = -EIO;
		if (unlikely(!(mref->ref_flags & MARS_REF_UPTODATE))) {
			MARS_ERR("not UPTODATE after initial read\n");
			goto out_err;
		}

		_usebuf_copy(mref_a, WRITE);

		// grab extra reference
		CHECK_ATOMIC(&origmref_a->subref_count, 1);
		atomic_inc(&origmref_a->subref_count);

		GENERIC_INPUT_CALL(input, mars_ref_io, mref, WRITE);
	} else {
		// finalize the read or final write
		if (likely(!cb->cb_error)) {
			struct bio *bio = origmref->orig_bio;
			int direction;
			status = -EINVAL;
			if (unlikely(!bio)) {
				MARS_ERR("bad bio setup on origmref %p", origmref);
				goto out_err;
			}
			direction = bio->bi_rw & 1;
			if (direction == READ) {
				_usebuf_copy(mref_a, READ);
			}
		}
	}

	CHECK_ATOMIC(&origmref_a->subref_count, 1);
	CHECK_ATOMIC(&mref->ref_count, 1);

	status = cb->cb_error;

out_err:	
	if (status < 0) {
		origmref->ref_cb->cb_error = status;
		MARS_ERR("error %d\n", status);
	}
	_usebuf_origmref_endio(output, origmref);

out_fatal: // no chance to call callback; this will result in mem leak :(
	;
}

////////////////// own brick / input / output operations //////////////////

static int usebuf_get_info(struct usebuf_output *output, struct mars_info *info)
{
	struct usebuf_input *input = output->brick->inputs[0];
	return GENERIC_INPUT_CALL(input, mars_get_info, info);
}

static int usebuf_ref_get(struct usebuf_output *output, struct mars_ref_object *origmref)
{
	MARS_FAT("not callable!\n");
	return -ENOSYS;
#if 0
	struct usebuf_input *input = output->brick->inputs[0];
	return GENERIC_INPUT_CALL(input, mars_ref_get, mref);
#endif
}

static void usebuf_ref_put(struct usebuf_output *output, struct mars_ref_object *origmref)
{
	CHECK_ATOMIC(&origmref->ref_count, 1);
	if (!atomic_dec_and_test(&origmref->ref_count)) {
		return;
	}
	usebuf_free_mars_ref(origmref);
#if 0 // NYI
	struct usebuf_input *input = output->brick->inputs[0];
	GENERIC_INPUT_CALL(input, mars_ref_put, mref);
#endif
}

static void usebuf_ref_io(struct usebuf_output *output, struct mars_ref_object *origmref, int rw)
{
	struct usebuf_input *input = output->brick->inputs[0];
	struct bio *bio = origmref->orig_bio;
	struct bio_vec *bvec;
	struct usebuf_mars_ref_aspect *origmref_a;
	loff_t start_pos;
	int start_len;
	int status;
	int i;
#if 1
	static int last_jiffies = 0;
	if (!last_jiffies || ((int)jiffies) - last_jiffies >= HZ * 30) {
		last_jiffies = jiffies;
		MARS_INF("USEBUF: reads=%d writes=%d prereads=%d\n", atomic_read(&output->io_count) - atomic_read(&output->write_count), atomic_read(&output->write_count), atomic_read(&output->preread_count));
	}
#endif

	MARS_DBG("START origmref=%p\n", origmref);

	status = -EINVAL;
	if (unlikely(!bio)) {
		MARS_ERR("cannot get bio\n");
		goto done;
	}
	origmref_a = usebuf_mars_ref_get_aspect(output, origmref);
	if (unlikely(!origmref_a)) {
		MARS_ERR("cannot get origmref_a\n");
		goto done;
	}

	origmref->ref_cb->cb_error = 0;

	// initial refcount: prevent intermediate drops
	_CHECK_ATOMIC(&origmref->ref_count, !=, 1);
	atomic_inc(&origmref->ref_count);

	_CHECK_ATOMIC(&origmref_a->subref_count, !=, 0);
	atomic_set(&origmref_a->subref_count, 1);

	start_pos = ((loff_t)bio->bi_sector) << 9; // TODO: make dynamic
	start_len = bio->bi_size;

	bio_for_each_segment(bvec, bio, i) {
		int this_len = bvec->bv_len;
		int my_offset = 0;

		while (this_len > 0) {
			struct mars_ref_object *mref;
			struct usebuf_mars_ref_aspect *mref_a;
			struct generic_callback *cb;
			int my_len;
			int my_rw;

			mref = usebuf_alloc_mars_ref(output, &output->ref_object_layout);
			status = -ENOMEM;
			if (unlikely(!mref)) {
				MARS_ERR("cannot alloc buffer, status=%d\n", status);
				goto done_drop;
			}

			mref->ref_pos = start_pos;
			mref->ref_len = this_len;
			mref->ref_may_write = rw;
			
			status = GENERIC_INPUT_CALL(input, mars_ref_get, mref);
			if (unlikely(status < 0)) {
				MARS_ERR("cannot get buffer, status=%d\n", status);
				goto done_drop;
			}
			my_len = status;
			MARS_DBG("origmref=%p got mref=%p pos=%lld len=%d mode=%d flags=%d status=%d\n", origmref, mref, start_pos, this_len, mref->ref_may_write, mref->ref_flags, status);

			status = -ENOMEM;
			mref_a = usebuf_mars_ref_get_aspect(output, mref);
			if (unlikely(!mref_a)) {
				MARS_ERR("cannot get my own mref aspect\n");
				goto put;
			}
			
			mref_a->origmref = origmref;
			mref_a->bvec = bvec;
			mref_a->bvec_offset = bvec->bv_offset + my_offset;
			mref_a->bvec_len = my_len;

			status = 0;
			if ((mref->ref_flags & MARS_REF_UPTODATE) && rw == READ) {
				// cache hit: immediately signal success
				_usebuf_copy(mref_a, READ);
				goto put;
			}
			
			cb = &mref_a->cb;
			cb->cb_fn = _usebuf_mref_endio;
			cb->cb_private = mref_a;
			cb->cb_error = 0;
			cb->cb_prev = NULL;
			mref_a->output = output;
			mref->ref_cb = cb;

			my_rw = rw;
			atomic_inc(&output->io_count);
			if (!(my_rw == READ)) {
				atomic_inc(&output->write_count);
				if (mref->ref_flags & MARS_REF_UPTODATE) {
					// buffer uptodate: start writing.
					_usebuf_copy(mref_a, WRITE);
				} else {
					// first start initial read, to get the whole buffer UPTODATE
					my_rw = READ;
					atomic_inc(&output->preread_count);
				}
			}

			// grab reference for each sub-IO
			CHECK_ATOMIC(&origmref_a->subref_count, 1);
			atomic_inc(&origmref_a->subref_count);

			GENERIC_INPUT_CALL(input, mars_ref_io, mref, my_rw);

		put:
			GENERIC_INPUT_CALL(input, mars_ref_put, mref);
			
			if (unlikely(status < 0))
				break;

			start_len -= my_len;
			start_pos += my_len;
			this_len -= my_len;
			my_offset += my_len;
		}
		if (unlikely(this_len != 0)) {
			MARS_ERR("bad internal length %d (status=%d)\n", this_len, status);
		}
	}

	if (unlikely(start_len != 0 && !status)) {
		MARS_ERR("length mismatch %d (status=%d)\n", start_len, status);
	}

done_drop:
	// drop initial refcount 
	if (status < 0)
		origmref->ref_cb->cb_error = status;
	_usebuf_origmref_endio(output, origmref);

done:
	MARS_DBG("status=%d\n", status);
}

//////////////// object / aspect constructors / destructors ///////////////

static int usebuf_mars_ref_aspect_init_fn(struct generic_aspect *_ini, void *_init_data)
{
	struct usebuf_mars_ref_aspect *ini = (void*)_ini;
	ini->origmref = NULL;
	ini->bvec = NULL;
	return 0;
}

static void usebuf_mars_ref_aspect_exit_fn(struct generic_aspect *_ini, void *_init_data)
{
	struct usebuf_mars_ref_aspect *ini = (void*)_ini;
	(void)ini;
}

MARS_MAKE_STATICS(usebuf);

////////////////////// brick constructors / destructors ////////////////////

static int usebuf_brick_construct(struct usebuf_brick *brick)
{
	return 0;
}

static int usebuf_output_construct(struct usebuf_output *output)
{
	return 0;
}

///////////////////////// static structs ////////////////////////

static struct usebuf_brick_ops usebuf_brick_ops = {
};

static struct usebuf_output_ops usebuf_output_ops = {
	.make_object_layout = usebuf_make_object_layout,
	.mars_get_info = usebuf_get_info,
	.mars_ref_get = usebuf_ref_get,
	.mars_ref_put = usebuf_ref_put,
	.mars_ref_io = usebuf_ref_io,
};

const struct usebuf_input_type usebuf_input_type = {
	.type_name = "usebuf_input",
	.input_size = sizeof(struct usebuf_input),
};

static const struct usebuf_input_type *usebuf_input_types[] = {
	&usebuf_input_type,
};

const struct usebuf_output_type usebuf_output_type = {
	.type_name = "usebuf_output",
	.output_size = sizeof(struct usebuf_output),
	.master_ops = &usebuf_output_ops,
	.output_construct = &usebuf_output_construct,
	.aspect_types = usebuf_aspect_types,
	.layout_code = {
		[BRICK_OBJ_MARS_REF] = LAYOUT_ALL,
	}
};

static const struct usebuf_output_type *usebuf_output_types[] = {
	&usebuf_output_type,
};

const struct usebuf_brick_type usebuf_brick_type = {
	.type_name = "usebuf_brick",
	.brick_size = sizeof(struct usebuf_brick),
	.max_inputs = 1,
	.max_outputs = 1,
	.master_ops = &usebuf_brick_ops,
	.default_input_types = usebuf_input_types,
	.default_output_types = usebuf_output_types,
	.brick_construct = &usebuf_brick_construct,
};
EXPORT_SYMBOL_GPL(usebuf_brick_type);

////////////////// module init stuff /////////////////////////

static int __init init_usebuf(void)
{
	printk(MARS_INFO "init_usebuf()\n");
	return usebuf_register_brick_type();
}

static void __exit exit_usebuf(void)
{
	printk(MARS_INFO "exit_usebuf()\n");
	usebuf_unregister_brick_type();
}

MODULE_DESCRIPTION("MARS usebuf brick");
MODULE_AUTHOR("Thomas Schoebel-Theuer <tst@1und1.de>");
MODULE_LICENSE("GPL");

module_init(init_usebuf);
module_exit(exit_usebuf);