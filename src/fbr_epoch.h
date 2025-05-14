/* See LICENSE file for copyright and license details. */

#ifndef _FBR_EPOCH_H
#define _FBR_EPOCH_H

#include <limits.h>
#include <stdint.h>

#include <ck_pr.h>

#include <fbr_errno.h>

#include "fbr_bm_alloc.h"
#include "fbr_cc.h"
#include "fbr_debug.h"

#define FBR_EPOCH_GRACE (2)

// Should be a power of two
#define FBR_EPOCH_TRY_ADVANCE_NTH ((unsigned int)(1 << 2))

struct fbr_epoch_entry {
	int active;
	unsigned int exit_count;
	uint64_t epoch;
};

struct fbr_epoch_entries {
	struct fbr_bm_alloc_meta meta;
	uint64_t epoch_global;
	struct fbr_epoch_entry *array;
};

/* = 0 -> a equals b
 * < 0 -> a is before b
 * > 0 -> a is after b
 */
FBR_ATTR_NO_SANITIZE_OVERFLOW
inline static int64_t fbr_epoch_cmp(uint64_t a, uint64_t b)
{
	return (int64_t)(a - b);
}

FBR_ATTR_NO_SANITIZE_OVERFLOW
inline static unsigned int fbr_epoch_exit_count_inc_fetch(struct fbr_epoch_entry *entry)
{
	entry->exit_count += 1;
	return entry->exit_count;
}

inline static fbr_errno_t fbr_epoch_entries_init(struct fbr_epoch_entries *e,
						 uint32_t num,
						 void *(*malloc)(size_t))
{
	fbr_assert(e != NULL);
	fbr_assert(malloc != NULL);
	fbr_assert(num > 0);

	struct fbr_epoch_entry *arr =
		fbr_bm_alloc_init(&e->meta, sizeof(*e->array), num, malloc);
	if (arr == NULL) {
		return FBR_ENOMEM;
	}
	e->epoch_global = FBR_EPOCH_GRACE;
	e->array = arr;
	for (uint32_t i = 0; i < num; ++i) {
		e->array[i].active = 0;
		e->array[i].exit_count = 0;
		e->array[i].epoch = FBR_EPOCH_GRACE;
	}

	return FBR_EOK;
}

inline static void fbr_epoch_entries_free(struct fbr_epoch_entries *e,
					  void (*free)(void *))
{
	fbr_assert(e != NULL);
	fbr_assert(free != NULL);

	fbr_bm_alloc_free(&e->meta, free);
}

inline static fbr_errno_t fbr_epoch_malloc(struct fbr_epoch_entries *e,
		struct fbr_epoch_entry **entry)
{
	fbr_errno_t err;
	uint32_t idx;
	uint64_t epoch;

	fbr_assert(e != NULL);
	fbr_assert(entry != NULL);

	err = fbr_bm_malloc(&e->meta, &idx);
	if (err != FBR_EOK) {
		return err;
	}
	*entry = &e->array[idx];
	return FBR_EOK;
}

inline static void fbr_epoch_free(struct fbr_epoch_entries *e, struct fbr_epoch_entry *entry)
{
	uint32_t idx;

	fbr_assert(e != NULL);
	fbr_assert(entry != NULL);
	fbr_assert(entry >= e->array);
	idx = (uint32_t)(entry - e->array);
	fbr_bm_free(&e->meta, idx);
}

inline static void fbr_epoch_enter(struct fbr_epoch_entries *e,
					  struct fbr_epoch_entry *entry)
{
	uint64_t epoch;

	fbr_assert(e != NULL);
	fbr_assert(entry != NULL);
	fbr_assert(entry >= e->array);

	(void)ck_pr_fas_int(&entry->active, 1);
	ck_pr_fence_atomic_load();
	epoch = ck_pr_load_64(&e->epoch_global);
	(void)ck_pr_fas_64(&entry->epoch, epoch);
}

inline static void fbr_epoch_exit(struct fbr_epoch_entries *e,
				  struct fbr_epoch_entry *entry)
{
	unsigned int exit_count;

	fbr_assert(e != NULL);
	fbr_assert(entry != NULL);
	fbr_assert(entry >= e->array);

	(void)ck_pr_fas_int(&entry->active, 0);
	exit_count = fbr_epoch_exit_count_inc_fetch(entry);

	if (exit_count & (FBR_EPOCH_TRY_ADVANCE_NTH - 1)) {
		return;
	}
	/* Check if we can advance the global epoch counter. We cannot
	 * advance if there is an active thread with an epoch lower than
	 * current global.
	 */
	uint64_t epoch_global = ck_pr_load_64(&e->epoch_global);
	uint64_t local_min = epoch_global + 1;
	uint32_t active_count = 0;
	uint32_t idx;
	struct fbr_bm_alloc_iterator iter;
	fbr_bm_iterator_init(&e->meta, &iter);
	while (fbr_bm_iterator_next(&e->meta, &iter, &idx)) {
		struct fbr_epoch_entry *local = &e->array[idx];
		int active = ck_pr_load_int(&local->active);
		if (!active) {
			continue;
		}
		ck_pr_fence_load();
		uint64_t local_epoch = ck_pr_load_64(&local->epoch);
		if (fbr_epoch_cmp(local_epoch, local_min) < 0) {
			local_min = local_epoch;
		}
		active_count += 1;
	}
	if (active_count == 0 || fbr_epoch_cmp(local_min, epoch_global) >= 0) {
		(void)ck_pr_cas_64(&e->epoch_global, epoch_global, epoch_global + 1);
	}
}

inline static uint64_t fbr_epoch_reclaim_max(struct fbr_epoch_entries *e)
{
	uint32_t idx;
	struct fbr_bm_alloc_iterator iter;
	uint64_t emin = FBR_EPOCH_GRACE;

	fbr_assert(e != NULL);

	fbr_bm_iterator_init(&e->meta, &iter);
	while (fbr_bm_iterator_next(&e->meta, &iter, &idx)) {
		int active;
		uint64_t epoch;
		struct fbr_epoch_entry *entry = &e->array[idx];
		active = ck_pr_load_int(&entry->active);
		if (!active) {
			continue;
		}
		epoch = ck_pr_load_64(&entry->epoch);
		if (epoch < emin) {
			emin = epoch;
		}
	}

	return emin - FBR_EPOCH_GRACE;
}

#endif /* _FBR_EPOCH_H */
