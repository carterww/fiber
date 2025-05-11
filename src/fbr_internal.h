/* See LICENSE file for copyright and license details. */

#ifndef _FBR_INTERNAL_H
#define _FBR_INTERNAL_H

#include "fbr_packed_counters.h"
#include <limits.h>

#include <fbr_new.h>

#include "fbr_bm_alloc.h"
#include "fbr_thread.h"

enum fbr_thread_type {
	FBR_THREAD_TYPE_NONE = 0,
	FBR_THREAD_TYPE_INTERNAL = 1,
	FBR_THREAD_TYPE_EXTERNAL = 2,
};

struct fbr_thread_internal {
	tid_t id;
	int canceled;
};

struct fbr_thread_external {
	unsigned long id;
};

struct fbr_thread {
	enum fbr_thread_type type;
	union {
		struct fbr_thread_internal internal;
		struct fbr_thread_external external;
	} thread;
};

struct fbr_thread_entries {
	struct fbr_bm_alloc_meta meta;
	struct fbr_thread *array;
};

struct fbr_pool {
	void *job_queue;
	struct fbr_queue_ops job_queue_ops;
	uint64_t job_id_counter;
	int active;

	unsigned int thread_num;
	union fbr_packed_counters_union twlo_qlhi;

	int thread_kill_num;

	struct fbr_thread_entries threads;

	const unsigned int thread_max; /* Max number of threads in pool */
	const unsigned int callers_max; /* Max number of callers */

	struct fbr_allocator alloc;
};

/* This is just for initialization. I keep it here so I'm reminded to change both */
struct fbr_pool_mutable {
	void *job_queue;
	struct fbr_queue_ops job_queue_ops;
	uint64_t job_id_counter;
	int active;

	unsigned int thread_num;
	union fbr_packed_counters_union twlo_qlhi;

	int thread_kill_num;

	struct fbr_thread_entries threads;

	unsigned int thread_max;
	unsigned int callers_max;

	struct fbr_allocator alloc;
};

inline static void fbr_free_sync(struct fbr_pool *pool)
{
	void *jq;
	void (*jq_free)(void *);
	struct fbr_bm_alloc_meta *meta;
	void (*alloc_free)(void *);

	fbr_assert(pool != NULL);

	jq = pool->job_queue;
	jq_free = pool->job_queue_ops.free;
	meta = &pool->threads.meta;
	alloc_free = pool->alloc.free;

	fbr_assert(jq != NULL);
	fbr_assert(jq_free != NULL);
	fbr_assert(alloc_free != NULL);

	/* This isn't strictly necessary but it will help make use
	 * after free bugs easier to find.
	 */
	ck_pr_store_ptr(&pool->job_queue, NULL);
	ck_pr_store_ptr(&pool->job_queue_ops.push, NULL);
	ck_pr_store_ptr(&pool->job_queue_ops.pop, NULL);
	ck_pr_store_ptr(&pool->job_queue_ops.init, NULL);
	ck_pr_store_ptr(&pool->job_queue_ops.free, NULL);
	ck_pr_store_ptr(&pool->threads.array, NULL);
	ck_pr_store_ptr(&pool->alloc.malloc, NULL);
	ck_pr_store_ptr(&pool->alloc.free, NULL);
	ck_pr_fence_memory();

	/* Free job queue */
	jq_free(jq);

	/* Free thread bm allocator */
	fbr_bm_alloc_free(meta, alloc_free);

	/* Free the pool */
	alloc_free(pool);
}

inline static bool fbr_pool_active(const struct fbr_pool *pool)
{
	fbr_assert(pool != NULL);
	return ck_pr_load_int(&pool->active) != 0;
}

#endif /* _FBR_INTERNAL_H */
