/* See LICENSE file for copyright and license details. */

#ifndef _FBR_JOB_H
#define _FBR_JOB_H

#include <stdint.h>

#include <ck_pr.h>

#include <fbr_errno.h>

#include "fbr_bm_alloc.h"
#include "fbr_debug.h"

struct fbr_job_entry {
	int active;
	uint64_t job_id;
};

struct fbr_job_entries {
	struct fbr_bm_alloc_meta meta;
	struct fbr_job_entry *array;
};

inline static fbr_errno_t fbr_job_entries_init(struct fbr_job_entries *j,
					       uint32_t thread_max,
					       void *(*malloc)(size_t))
{
	fbr_assert(j != NULL);
	fbr_assert(malloc != NULL);
	fbr_assert(thread_max > 0);

	struct fbr_job_entry *arr = fbr_bm_alloc_init(
		&j->meta, sizeof(*j->array), thread_max, malloc);
	if (arr == NULL) {
		return FBR_ENOMEM;
	}
	j->array = arr;
	for (uint32_t i = 0; i < thread_max; ++i) {
		j->array[i].active = 0;
	}

	return FBR_EOK;
}

inline static void fbr_job_entries_free(struct fbr_job_entries *j,
					void (*free)(void *))
{
	fbr_assert(j != NULL);
	fbr_assert(free != NULL);

	fbr_bm_alloc_free(&j->meta, free);
}

inline static fbr_errno_t fbr_job_entry_malloc(struct fbr_job_entries *j,
					       struct fbr_job_entry **entry)
{
	fbr_errno_t err;
	uint32_t idx;

	fbr_assert(j != NULL);
	fbr_assert(entry != NULL);

	err = fbr_bm_malloc(&j->meta, &idx);
	if (err == FBR_ENOMEM) {
		return err;
	}
	fbr_assert(err == FBR_EOK);
	*entry = &j->array[idx];
	return FBR_EOK;
}

inline static void fbr_job_entry_free(struct fbr_job_entries *j,
				      struct fbr_job_entry *entry)
{
	uint32_t idx;
	int active_prev;

	fbr_assert(j != NULL);

	// This is just a debugging statement. The caller should already
	// do this
	active_prev = ck_pr_fas_int(&entry->active, 0);
	fbr_assert(active_prev == 0);
	ck_pr_barrier();
	idx = (uint32_t)(entry - j->array);
	fbr_bm_free(&j->meta, idx);
}

inline static bool fbr_job_executing(struct fbr_job_entries *j, uint64_t job_id)
{
	uint32_t idx;
	struct fbr_bm_alloc_iterator iter;

	fbr_bm_iterator_init(&j->meta, &iter);
	while (fbr_bm_iterator_next(&j->meta, &iter, &idx)) {
		struct fbr_job_entry *entry = &j->array[idx];
		int active = ck_pr_load_int(&entry->active);
		if (!active) {
			continue;
		}
		uint64_t local_job_id = ck_pr_load_64(&entry->job_id);
		if (local_job_id == job_id) {
			return true;
		}
	}
	return false;
}

#endif /* _FBR_JOB_H */
