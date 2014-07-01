/*  (c) 2012 Thomas Schoebel-Theuer / 1&1 Internet AG */
#ifndef MARS_LIB_MAPFREE_H
#define MARS_LIB_MAPFREE_H

/* Mapfree infrastructure.
 *
 * Purposes:
 *
 * 1) Open files only once when possible, do ref-counting on struct mapfree_info
 *
 * 2) Automatically call invalidate_mapping_pages() in the background on
 *    "unused" areas to free resources.
 *    Used areas can be indicated by calling mapfree_set() frequently.
 *    Usage model: tailored to sequential logfiles.
 *
 * 3) Do it all in a completely decoupled manner, in order to prevent resource deadlocks.
 *
 * 4) Also to prevent deadlocks: always set mapping_set_gfp_mask() accordingly.
 */

#include "mars.h"

extern int mapfree_period_sec;
extern int mapfree_grace_keep_mb;

struct mapfree_info {
	struct list_head mf_head;
	struct list_head mf_dirty_anchor;
	char		*mf_name;
	struct file	*mf_filp;
	int		 mf_flags;
	atomic_t	 mf_count;
	spinlock_t	 mf_lock;
	loff_t		 mf_min[2];
	loff_t		 mf_last;
	loff_t		 mf_max;
	long long	 mf_jiffies;
};

struct dirty_info {
	struct list_head dirty_head;
	struct aio_object *dirty_aio;
	int dirty_stage;
};

struct mapfree_info *mapfree_get(const char *filename, int flags);

void mapfree_put(struct mapfree_info *mf);

void mapfree_set(struct mapfree_info *mf, loff_t min, loff_t max);

/***************** dirty IOs on the fly  *****************/

void mf_insert_dirty(struct mapfree_info *mf, struct dirty_info *di);
void mf_remove_dirty(struct mapfree_info *mf, struct dirty_info *di);
void mf_get_dirty(struct mapfree_info *mf, loff_t *min, loff_t *max, int min_stage, int max_stage);
void mf_get_any_dirty(const char *filename, loff_t *min, loff_t *max, int min_stage, int max_stage);

/***************** module init stuff ************************/

int __init init_mars_mapfree(void);

void exit_mars_mapfree(void);

#endif
