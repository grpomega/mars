// (c) 2010 Thomas Schoebel-Theuer / 1&1 Internet AG
#ifndef MARS_BUF_H
#define MARS_BUF_H

#include <linux/list.h>
#include <asm/atomic.h>

//#define MARS_BUF_HASH_MAX 512
#define MARS_BUF_HASH_MAX 2048

struct buf_mars_ref_aspect {
	GENERIC_ASPECT(mars_ref);
	struct buf_head *rfa_bf;
	struct list_head rfa_pending_head;
	struct list_head tmp_head;
	struct generic_callback cb;
};

struct cache_anchor {
	spinlock_t       hash_lock;
	struct list_head hash_anchor;
};

struct buf_brick {
	MARS_BRICK(buf);
	/* brick parameters */
	int backing_order;
	int backing_size;
	int max_count;
	
	/* internals */
	spinlock_t brick_lock;
	atomic_t alloc_count;
	atomic_t hashed_count;
	atomic_t lru_count;
	atomic_t nr_io_pending;
	atomic_t nr_collisions;
	struct generic_object_layout mref_object_layout;

	// lists for caching
	struct list_head free_anchor;  // members are not hashed
	struct list_head lru_anchor;   // members are hashed and not in use
	struct cache_anchor cache_anchors[MARS_BUF_HASH_MAX]; // hash table

	// for creation of bios
	struct mars_info base_info;
	struct block_device *bdev;
	int bvec_max;

	// statistics
	unsigned long last_jiffies;
	atomic_t hit_count;
	atomic_t miss_count;
	atomic_t io_count;
};

struct buf_input {
	MARS_INPUT(buf);
};

struct buf_output {
	MARS_OUTPUT(buf);
};

MARS_TYPES(buf);

struct buf_head {
	void             *bf_data;
	spinlock_t        bf_lock;
	struct buf_brick *bf_brick;
	loff_t            bf_pos;
	loff_t            bf_base_index;
	int               bf_flags;
	atomic_t          bf_count;
	int               bf_bio_status;
	atomic_t          bf_bio_count;
	// lists for caching
	//struct list_head bf_mref_anchor; // all current mref members
	struct list_head  bf_lru_head;
	struct list_head  bf_hash_head;
	// lists for IO
	struct list_head  bf_io_pending_anchor;
	struct list_head  bf_postpone_anchor;
};

#endif