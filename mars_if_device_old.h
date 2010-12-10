// (c) 2010 Thomas Schoebel-Theuer / 1&1 Internet AG
#ifndef MARS_IF_DEVICE_H
#define MARS_IF_DEVICE_H

#define HT_SHIFT 6 //????
#define MARS_MAX_SEGMENT_SIZE (1U << (9+HT_SHIFT))

struct if_device_mars_ref_aspect {
	GENERIC_ASPECT(mars_ref);
	struct list_head tmp_head;
	struct if_device_mars_ref_aspect *master;
	atomic_t split_count;
	struct generic_callback cb;
	struct bio *orig_bio;
	struct if_device_input *input;
};

struct if_device_input {
	MARS_INPUT(if_device);
	struct request_queue *q;
	struct gendisk *disk;
	struct block_device *bdev;
	spinlock_t req_lock;
	struct generic_object_layout mref_object_layout;
};

struct if_device_output {
	MARS_OUTPUT(if_device);
};

struct if_device_brick {
	MARS_BRICK(if_device);
	bool is_active;
	struct if_device_output hidden_output;
};

MARS_TYPES(if_device);

#endif