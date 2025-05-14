/* See LICENSE file for copyright and license details. */

#ifndef _FBR_WAIT_H
#define _FBR_WAIT_H

#include <stdint.h>

#include <ck_pr.h>

#include <fbr_errno.h>

#include "fbr_bm_alloc.h"
#include "fbr_cc.h"
#include "fbr_debug.h"
#include "fbr_epoch.h"

enum fbr_wait_entry_status {
	FBR_WAIT_ENTRY_INACTIVE = 0,
	FBR_WAIT_ENTRY_ACTIVE = 1,
	FBR_WAIT_ENTRY_RETIRED = 2,
};

struct fbr_wait_entry {
	enum fbr_wait_entry_status status;
	uint32_t futex;
	uint64_t timestamp;
	uint64_t epoch_retired;
};

struct fbr_wait_entries {
	struct fbr_bm_alloc_meta meta;
	uint64_t timestamp_global;
	struct fbr_wait_entry *array;
	uint32_t retired_approx;
	int reclaim_flag;
};

/* = 0 -> a equals b
 * < 0 -> a is before b
 * > 0 -> a is after b
 */
FBR_ATTR_NO_SANITIZE_OVERFLOW
inline static int64_t fbr_timestamp_cmp(uint64_t a, uint64_t b)
{
	return (int64_t)(a - b);
}

inline static fbr_errno_t fbr_wait_entries_init(struct fbr_wait_entries *w,
						uint32_t num,
						void *(*malloc)(size_t))
{
	fbr_assert(w != NULL);
	fbr_assert(malloc != NULL);
	fbr_assert(num > 0);

	struct fbr_wait_entry *arr =
		fbr_bm_alloc_init(&w->meta, sizeof(*w->array), num, malloc);
	if (arr == NULL) {
		return FBR_ENOMEM;
	}
	w->timestamp_global = 0;
	w->array = arr;
	w->retired_approx = 0;
	w->reclaim_flag = 0;
	for (uint32_t i = 0; i < num; ++i) {
		w->array[i].status = FBR_WAIT_ENTRY_INACTIVE;
		w->array[i].futex = 0;
		w->array[i].timestamp = 0;
		w->array[i].epoch_retired = FBR_EPOCH_GRACE;
	}

	return FBR_EOK;
}

inline static void fbr_wait_entries_free(struct fbr_wait_entries *w,
					 void (*free)(void *))
{
	fbr_assert(w != NULL);
	fbr_assert(free != NULL);

	fbr_bm_alloc_free(&w->meta, free);
}

inline static bool fbr_wait_entries_reclaim_start(struct fbr_wait_entries *w)
{
	int reclaim_flag_prev;

	reclaim_flag_prev = ck_pr_fas_int(&w->reclaim_flag, 1);
	return reclaim_flag_prev == 0;
}

inline static void fbr_wait_entries_reclaim_finish(struct fbr_wait_entries *w)
{
	int reclaim_flag_prev;

	reclaim_flag_prev = ck_pr_fas_int(&w->reclaim_flag, 0);
	fbr_assert(reclaim_flag_prev == 1);
}

inline static fbr_errno_t fbr_wait_entry_add(struct fbr_wait_entries *w,
					     uint32_t **futex)
{
	fbr_errno_t err;
	uint32_t idx;
	uint64_t timestamp;
	int prev_status;
	struct fbr_wait_entry *entry;

	fbr_assert(w != NULL);
	fbr_assert(futex != NULL);

	err = fbr_bm_malloc(&w->meta, &idx);
	if (err != FBR_EOK) {
		return err;
	}
	entry = &w->array[idx];

	timestamp = ck_pr_faa_64(&w->timestamp_global, 1);
	ck_pr_fence_atomic_store();
	ck_pr_store_64(&entry->timestamp, timestamp);
	ck_pr_fence_store_atomic();
	prev_status = ck_pr_fas_int((int *)&entry->status,
				    (int)FBR_WAIT_ENTRY_ACTIVE);
	fbr_assert((enum fbr_wait_entry_status)prev_status ==
		   FBR_WAIT_ENTRY_INACTIVE);
	*futex = &entry->futex;

	return FBR_EOK;
}

inline static uint32_t fbr_wait_entry_retire(struct fbr_wait_entries *w,
					     uint64_t epoch, uint32_t idx)
{
	int prev_status;
	enum fbr_wait_entry_status stat;
	struct fbr_wait_entry *entry;

	fbr_assert(w != NULL);
	fbr_assert(idx < w->meta.bm->n_bits);

	entry = &w->array[idx];

	ck_pr_store_64(&entry->epoch_retired, epoch);
	ck_pr_fence_store_atomic();
	prev_status = ck_pr_fas_int((int *)&entry->status,
				    (int)FBR_WAIT_ENTRY_RETIRED);
	stat = (enum fbr_wait_entry_status)prev_status;
	fbr_assert(stat == FBR_WAIT_ENTRY_ACTIVE ||
		   stat == FBR_WAIT_ENTRY_RETIRED);
	if (stat == FBR_WAIT_ENTRY_ACTIVE) {
		ck_pr_fence_atomic();
		return ck_pr_faa_32(&w->retired_approx, 1) + 1;
	} else {
		ck_pr_fence_atomic_load();
		return ck_pr_load_32(&w->retired_approx);
	}
}

inline static void fbr_wait_entry_free(struct fbr_wait_entries *w, uint32_t idx,
				       void (*free)(void *))
{
	int prev_status;
	enum fbr_wait_entry_status stat;
	struct fbr_wait_entry *entry;

	fbr_assert(w != NULL);
	fbr_assert(idx < w->meta.bm->n_bits);
	fbr_assert(free != NULL);

	entry = &w->array[idx];

	prev_status = ck_pr_fas_int((int *)&entry->status,
				    (int)FBR_WAIT_ENTRY_INACTIVE);
	stat = (enum fbr_wait_entry_status)prev_status;
	fbr_assert(stat == FBR_WAIT_ENTRY_RETIRED);
	ck_pr_fence_atomic();
	ck_pr_dec_32(&w->retired_approx);
	ck_pr_barrier();
	fbr_bm_alloc_free(&w->meta, free);
}

inline static void fbr_wait_entries_reclaim(struct fbr_wait_entries *w,
					    uint64_t epoch_global,
					    void (*free)(void *))
{
	struct fbr_bm_alloc_iterator iter;
	uint32_t idx;
	uint64_t epoch_thresh;

	if (!fbr_wait_entries_reclaim_start(w)) {
		return;
	}

	epoch_thresh = epoch_global - FBR_EPOCH_GRACE;
	fbr_bm_iterator_init(&w->meta, &iter);
	while (fbr_bm_iterator_next(&w->meta, &iter, &idx)) {
		int istatus;
		enum fbr_wait_entry_status status;
		uint64_t entry_epoch;
		struct fbr_wait_entry *wait_entry = &w->array[idx];
		istatus = ck_pr_load_int((int *)&wait_entry->status);
		status = (enum fbr_wait_entry_status)istatus;
		if (status != FBR_WAIT_ENTRY_RETIRED) {
			continue;
		}
		entry_epoch = ck_pr_load_64(&wait_entry->epoch_retired);
		if (fbr_epoch_cmp(epoch_thresh, entry_epoch) < 0) {
			continue;
		}
		fbr_wait_entry_free(w, idx, free);
	}
	ck_pr_barrier();
	fbr_wait_entries_reclaim_finish(w);
}

#endif /* _FBR_WAIT_H */
