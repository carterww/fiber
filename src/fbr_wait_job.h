// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Carter Williams

#ifndef _FBR_WAIT_JOB_H
#define _FBR_WAIT_JOB_H

#include <stdint.h>

#include <ck_pr.h>

#include <fbr.h>
#include <fbr_errno.h>

#include "fbr_bm_alloc.h"
#include "fbr_debug.h"
#include "fbr_hp.h"
#include "fbr_futex.h"
#include "fbr_wait.h"

#define FBR_WAIT_JOB_HPS_PER_THREAD (1)
#define FBR_WAIT_JOB_CONCURRENT_RECLAIMERS_MAX (2)

struct fbr_wait_job_entry {
	enum fbr_wait_entry_status status;
	uint32_t futex;
	uint64_t job_id;
};

struct fbr_wait_job_entries {
	struct fbr_bm_alloc_meta meta;
	struct fbr_wait_job_entry *array;
	uint32_t retired_approx;
	int hp_cache_taken[FBR_WAIT_CONCURRENT_RECLAIMERS_MAX];
	void **hp_cache[FBR_WAIT_CONCURRENT_RECLAIMERS_MAX];
};

inline static enum fbr_wait_entry_status
fbr_wait_job_entry_status_get(struct fbr_wait_job_entry *e)
{
	int istatus;
	enum fbr_wait_entry_status status;

	istatus = ck_pr_load_int((int *)&e->status);
	status = (enum fbr_wait_entry_status)istatus;

	return status;
}

inline static size_t fbr_wait_job_entries_size_bm(uint32_t entries)
{
	return fbr_bm_alloc_size(entries, sizeof(struct fbr_wait_job_entry));
}

inline static size_t fbr_wait_job_entries_size_hp_cache(uint32_t hp_entries)
{
	size_t hp_cache_size;
	hp_cache_size = sizeof(void *) * hp_entries * FBR_WAIT_HPS_PER_THREAD *
			FBR_WAIT_CONCURRENT_RECLAIMERS_MAX;
	hp_cache_size = FBR_SIZE_ROUND_CACHELINE(hp_cache_size);
	return hp_cache_size;
}

inline static size_t fbr_wait_job_entries_size(uint32_t entries,
					       uint32_t hp_entries)
{
	size_t bm_alloc_size = fbr_wait_job_entries_size_bm(entries);
	size_t hp_cache_size = fbr_wait_job_entries_size_hp_cache(hp_entries);
	return bm_alloc_size + hp_cache_size;
}

inline static void fbr_wait_job_entries_init(struct fbr_wait_job_entries *w,
					     uint32_t num, uint32_t hp_entries,
					     void *buffer, size_t buffer_size)
{
	void **hp_cache_ptr;
	size_t bm_size;
	size_t cache_size;

	fbr_assert(w != NULL);
	fbr_assert(num > 0);
	fbr_assert(hp_entries > 0);
	fbr_assert(buffer != NULL);

	bm_size = fbr_wait_job_entries_size_bm(num);
	cache_size = fbr_wait_job_entries_size_hp_cache(hp_entries);
	if (buffer_size < bm_size + cache_size) {
		fbr_unreachable();
	}

	struct fbr_wait_job_entry *arr = fbr_bm_alloc_init(
		&w->meta, sizeof(*w->array), num, buffer, bm_size);
	fbr_assert(arr != NULL);
	w->array = arr;
	w->retired_approx = 0;
	w->hp_cache[0] = NULL;
	for (uint32_t i = 0; i < num; ++i) {
		w->array[i].status = FBR_WAIT_ENTRY_INACTIVE;
		w->array[i].futex = 0;
	}
	hp_cache_ptr = (void *)((uintptr_t)buffer + bm_size);
	fbr_assert(fbr_aligned(hp_cache_ptr, FBR_ALIGNMENT_MIN));
	for (uint32_t i = 0; i < FBR_WAIT_JOB_CONCURRENT_RECLAIMERS_MAX; ++i) {
		w->hp_cache[i] = hp_cache_ptr +
				 (i * hp_entries * FBR_WAIT_JOB_HPS_PER_THREAD);
		w->hp_cache_taken[i] = 0;
	}
}

inline static fbr_errno_t
fbr_wait_job_entry_add(struct fbr_wait_job_entries *w, struct fbr_hp_entry *hp,
		       uint64_t job_id, struct fbr_wait_job_entry **entry_out)
{
	fbr_errno_t err;
	uint32_t idx;
	int prev_status;
	struct fbr_wait_job_entry *entry;

	fbr_assert(w != NULL);
	fbr_assert(hp != NULL);
	fbr_assert(entry_out != NULL);

	err = fbr_bm_malloc(&w->meta, &idx);
	if (err != FBR_EOK) {
		return err;
	}
	entry = &w->array[idx];

	ck_pr_store_ptr(&hp->ptrs[0], entry);
	ck_pr_store_64(&entry->job_id, job_id);
	ck_pr_fence_store_atomic();
	prev_status = ck_pr_fas_int((int *)&entry->status,
				    (int)FBR_WAIT_ENTRY_ACTIVE);
	fbr_assert((enum fbr_wait_entry_status)prev_status ==
		   FBR_WAIT_ENTRY_INACTIVE);
	*entry_out = entry;

	return FBR_EOK;
}

inline static uint32_t
fbr_wait_job_entry_retire(struct fbr_wait_job_entries *w,
			  struct fbr_wait_job_entry *entry)
{
	int prev_status;
	enum fbr_wait_entry_status stat;

	fbr_assert(w != NULL);
	fbr_assert(entry >= w->array);

	prev_status = ck_pr_fas_int((int *)&entry->status,
				    (int)FBR_WAIT_ENTRY_RETIRED);
	stat = (enum fbr_wait_entry_status)prev_status;
	fbr_assert(stat == FBR_WAIT_ENTRY_ACTIVE ||
		   stat == FBR_WAIT_ENTRY_RETIRED);
	if (stat == FBR_WAIT_ENTRY_ACTIVE) {
		return ck_pr_faa_32(&w->retired_approx, 1) + 1;
	} else {
		return ck_pr_load_32(&w->retired_approx);
	}
}

inline static void fbr_wait_job_entry_free(struct fbr_wait_job_entries *w,
					   struct fbr_wait_job_entry *entry)
{
	uint32_t idx;

	fbr_assert(w != NULL);
	fbr_assert(entry >= w->array);

	if (ck_pr_cas_int((int *)&entry->status, (int)FBR_WAIT_ENTRY_RETIRED,
			  (int)FBR_WAIT_ENTRY_INACTIVE)) {
		ck_pr_store_32(&entry->futex, 0);
		ck_pr_fence_store_atomic();
		ck_pr_dec_32(&w->retired_approx);
		ck_pr_barrier();
		idx = (uint32_t)(entry - w->array);
		fbr_assert(idx < w->meta.bm->n_bits);
		fbr_bm_free(&w->meta, idx);
	}
}

inline static void
fbr_wait_job_entries_reclaim(struct fbr_wait_job_entries *w,
			     struct fbr_hp_entries *hp_entries,
			     struct fbr_hp_entry *hp)
{
	int reclaim_idx = -1;
	void **hp_cache;
	uint32_t hp_count;
	struct fbr_bm_alloc_iterator iter;
	uint32_t idx;

	for (int i = 0; i < FBR_WAIT_JOB_CONCURRENT_RECLAIMERS_MAX; ++i) {
		int taken = ck_pr_fas_int(&w->hp_cache_taken[i], 1);
		if (!taken) {
			reclaim_idx = i;
			break;
		}
	}
	if (reclaim_idx < 0) {
		return;
	}
	hp_cache = w->hp_cache[reclaim_idx];
	hp_count = fbr_hp_gather_single(hp_entries, hp_cache);

	fbr_bm_iterator_init(&w->meta, &iter);
loop:
	while (fbr_bm_iterator_next(&w->meta, &iter, &idx)) {
		enum fbr_wait_entry_status status;
		uint64_t entry_timestamp;

		struct fbr_wait_job_entry *entry = &w->array[idx];
		fbr_hp_post(hp, entry, 0);
		status = fbr_wait_job_entry_status_get(entry);
		if (status != FBR_WAIT_ENTRY_RETIRED) {
			continue;
		}
		for (uint32_t i = 0; i < hp_count; ++i) {
			if (hp_cache[i] == entry) {
				goto loop;
			}
		}
		fbr_wait_job_entry_free(w, entry);
	}
	fbr_hp_clear(hp);
	ck_pr_store_int(&w->hp_cache_taken[reclaim_idx], 0);
	ck_pr_fence_memory();
}

inline static uint32_t fbr_wait_job_entries_wake(struct fbr_wait_job_entries *w,
						 struct fbr_hp_entry *hp,
						 uint64_t job_id)
{
	struct fbr_bm_alloc_iterator iter;
	uint32_t idx;
	uint32_t retired_approx;

	fbr_assert(w != NULL);
	fbr_assert(hp != NULL);

	// Atomic load not necessary, nobody can change this rn
	retired_approx = 0;
	fbr_bm_iterator_init(&w->meta, &iter);
	while (fbr_bm_iterator_next(&w->meta, &iter, &idx)) {
		uint32_t nwake = 1;
		enum fbr_wait_entry_status status;
		uint64_t local_job_id;

		struct fbr_wait_job_entry *entry = &w->array[idx];
		fbr_hp_post(hp, entry, 0);
		status = fbr_wait_job_entry_status_get(entry);
		if (status != FBR_WAIT_ENTRY_ACTIVE) {
			continue;
		}
		local_job_id = ck_pr_load_64(&entry->job_id);
		if (local_job_id != job_id) {
			continue;
		}
		(void)ck_pr_fas_32(&entry->futex, 1);
		ck_pr_barrier();
		fbr_errno_t err = fbr_futex_wake(&entry->futex, &nwake);
		fbr_assert(err == FBR_EOK);
		retired_approx = fbr_wait_job_entry_retire(w, entry);
	}
	fbr_hp_clear(hp);
	return retired_approx;
}

#endif /* _FBR_WAIT_JOB_H */
