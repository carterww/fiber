/* See LICENSE file for copyright and license details. */

#ifndef _FBR_HP_H
#define _FBR_HP_H

#include <limits.h>
#include <stdint.h>

#include <ck_pr.h>

#include <fbr_errno.h>

#include "fbr_bm_alloc.h"
#include "fbr_debug.h"
#include "fbr_platform.h"

#define FBR_HP_PTRS_PER_CACHELINE \
	((uint32_t)(FBR_CACHELINE_BYTES / sizeof(void *)))

struct fbr_hp_entry {
	void *ptrs[FBR_HP_PTRS_PER_CACHELINE];
};

struct fbr_hp_entries {
	struct fbr_bm_alloc_meta meta;
	struct fbr_hp_entry *array;
};

inline static uint32_t fbr_hp_buffer_len(uint32_t thread_max,
					 uint32_t callers_max,
					 uint32_t hp_per_thread)
{
	return (thread_max + callers_max) * hp_per_thread * 2;
}

inline static void fbr_hp_post(struct fbr_hp_entry *entry, void *val,
			       uint32_t hp_n)
{
	fbr_assert(hp_n < FBR_HP_PTRS_PER_CACHELINE);

	ck_pr_store_ptr(&entry->ptrs[hp_n], val);
	ck_pr_fence_memory();
}

inline static void fbr_hp_inherit(struct fbr_hp_entry *entry, uint32_t hp_from,
				  uint32_t hp_to)
{
	void *val;

	fbr_assert(hp_from < hp_to);

	val = ck_pr_load_ptr(&entry->ptrs[hp_from]);
	ck_pr_store_ptr(&entry->ptrs[hp_to], val);
	ck_pr_fence_memory();
}

inline static void fbr_hp_clear(struct fbr_hp_entry *entry)
{
	for (uint32_t i = 0; i < FBR_HP_PTRS_PER_CACHELINE; ++i) {
		ck_pr_store_ptr(&entry->ptrs[i], NULL);
	}
}

inline static fbr_errno_t fbr_hp_entries_init(struct fbr_hp_entries *hp,
					      uint32_t num,
					      void *(*malloc)(size_t))
{
	fbr_assert(hp != NULL);
	fbr_assert(num > 0);
	fbr_assert(malloc != NULL);

	struct fbr_hp_entry *arr =
		fbr_bm_alloc_init(&hp->meta, sizeof(*hp->array), num, malloc);
	if (arr == NULL) {
		return FBR_ENOMEM;
	}
	hp->array = arr;
	for (uint32_t i = 0; i < num; ++i) {
		fbr_hp_clear(&hp->array[i]);
	}

	return FBR_EOK;
}

inline static void fbr_hp_entries_free(struct fbr_hp_entries *hp,
				       void (*free)(void *))
{
	fbr_assert(hp != NULL);
	fbr_assert(free != NULL);

	fbr_bm_alloc_free(&hp->meta, free);
}

inline static fbr_errno_t fbr_hp_malloc(struct fbr_hp_entries *hp,
					struct fbr_hp_entry **entry)
{
	fbr_errno_t err;
	uint32_t idx;
	uint64_t epoch;

	fbr_assert(hp != NULL);
	fbr_assert(entry != NULL);

	err = fbr_bm_malloc(&hp->meta, &idx);
	if (err != FBR_EOK) {
		return err;
	}
	*entry = &hp->array[idx];
	return FBR_EOK;
}

inline static void fbr_hp_free(struct fbr_hp_entries *hp,
			       struct fbr_hp_entry *entry)
{
	uint32_t idx;

	fbr_assert(hp != NULL);
	fbr_assert(entry != NULL);
	fbr_assert(entry >= hp->array);

	fbr_hp_clear(entry);

	idx = (uint32_t)(entry - hp->array);
	fbr_bm_free(&hp->meta, idx);
}

inline static uint32_t fbr_hp_gather_single(struct fbr_hp_entries *hp,
					    void **cache)
{
	uint32_t idx;
	struct fbr_bm_alloc_iterator iter;
	uint32_t count = 0;

	fbr_bm_iterator_init(&hp->meta, &iter);
	while (fbr_bm_iterator_next(&hp->meta, &iter, &idx)) {
		struct fbr_hp_entry *entry = &hp->array[idx];
		void *ptr = ck_pr_load_ptr(&entry->ptrs[0]);
		if (ptr == NULL) {
			continue;
		}
		cache[count] = ptr;
		count += 1;
	}
	return count;
}

#endif /* _FBR_HP_H */
