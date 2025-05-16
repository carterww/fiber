/* See LICENSE file for copyright and license details. */

#ifndef _FBR_THREAD_ENTRIES_H
#define _FBR_THREAD_ENTRIES_H

#include <stdint.h>

#include <ck_pr.h>

#include <fbr_errno.h>

#include "fbr_bm_alloc.h"
#include "fbr_debug.h"
#include "fbr_thread.h"

enum fbr_thread_type {
	FBR_THREAD_TYPE_NONE = 0,
	FBR_THREAD_TYPE_INTERNAL = 1,
	FBR_THREAD_TYPE_EXTERNAL = 2,
};

struct fbr_thread_internal {
	tid_t id;
};

struct fbr_thread_external {
	uint64_t id;
};

struct fbr_thread {
	enum fbr_thread_type type;
	int started;
	union {
		struct fbr_thread_internal internal;
		struct fbr_thread_external external;
	} thread;
	struct fbr_pool *pool;
	unsigned int thread_idx;
};

struct fbr_thread_entries {
	struct fbr_bm_alloc_meta meta;
	struct fbr_thread *array;
};

inline static fbr_errno_t fbr_thread_entries_init(struct fbr_thread_entries *te,
						  uint32_t thread_max,
						  void *(*malloc)(size_t))
{
	fbr_assert(te != NULL);
	fbr_assert(malloc != NULL);
	fbr_assert(thread_max > 0);

	struct fbr_thread *arr = fbr_bm_alloc_init(
		&te->meta, sizeof(*te->array), thread_max, malloc);
	if (arr == NULL) {
		return FBR_ENOMEM;
	}
	te->array = arr;
	for (uint32_t i = 0; i < thread_max; ++i) {
		te->array[i].type = FBR_THREAD_TYPE_NONE;
		te->array[i].started = 0;
	}

	return FBR_EOK;
}

inline static void fbr_thread_entries_free(struct fbr_thread_entries *te,
					   void (*free)(void *))
{
	fbr_assert(te != NULL);
	fbr_assert(free != NULL);

	fbr_bm_alloc_free(&te->meta, free);
}

inline static uint32_t
fbr_thread_entry_ptr_to_idx(struct fbr_thread_entries *te, struct fbr_thread *t)
{
	fbr_assert(t >= te->array);
	fbr_assert(t < &te->array[te->meta.bm->n_bits]);
	return (uint32_t)(t - te->array);
}

inline static bool fbr_thread_entry_get_internal(struct fbr_thread_entries *te,
						 const tid_t *tid,
						 uint32_t *idx)
{
	struct fbr_bm_alloc_iterator iter;

	fbr_bm_iterator_init(&te->meta, &iter);

	while (fbr_bm_iterator_next(&te->meta, &iter, idx)) {
		int type_int;
		enum fbr_thread_type type;
		struct fbr_thread *entry = &te->array[*idx];
		type_int = ck_pr_load_int((int *)&entry->type);
		type = (enum fbr_thread_type)type_int;
		if (type == FBR_THREAD_TYPE_INTERNAL &&
		    fbr_thread_tid_equal(tid, &entry->thread.internal.id)) {
			return true;
		}
	}
	return false;
}

inline static fbr_errno_t fbr_thread_entry_malloc(struct fbr_thread_entries *te,
						  enum fbr_thread_type type,
						  uint32_t *idx)
{
	fbr_errno_t err;
	struct fbr_thread *t;

	fbr_assert(te != NULL);
	fbr_assert(idx != NULL);

	err = fbr_bm_malloc(&te->meta, idx);
	if (err == FBR_ENOMEM) {
		return err;
	}
	fbr_assert(err == FBR_EOK);
	t = &te->array[*idx];
	ck_pr_store_int((int *)&t->type, (int)type);
	ck_pr_fence_memory();
	return FBR_EOK;
}

inline static void fbr_thread_entry_free(struct fbr_thread_entries *te,
					 uint32_t idx)
{
	struct fbr_thread *t;
	int type_int;
	enum fbr_thread_type type;

	fbr_assert(te != NULL);

	t = &te->array[idx];
	type_int = ck_pr_load_int((int *)&t->type);
	type = (enum fbr_thread_type)type_int;
	ck_pr_store_int(&t->started, 0);
	ck_pr_fence_store();
	ck_pr_store_int((int *)&t->type, (int)FBR_THREAD_TYPE_NONE);
	ck_pr_fence_memory();
	fbr_bm_free(&te->meta, idx);
}

#endif /* _FBR_THREAD_ENTRIES_H */
