/* See LICENSE file for copyright and license details. */

#ifndef _FBR_INTERNAL_H
#define _FBR_INTERNAL_H

#include <limits.h>

#include <fbr.h>

#include "fbr_cc.h"
#include "fbr_thread_entries.h"

struct fbr_tw_ql_packed {
	uint32_t thread_working;
	uint32_t queue_length;
} FBR_ATTR_ALIGNED(8) FBR_ATTR_PACKED;

struct fbr_pool {
	void *job_queue;
	struct fbr_queue_ops job_queue_ops;
	int active;

	unsigned int thread_num;
	struct fbr_tw_ql_packed tw_ql;

	int32_t thread_kill_num;

	struct fbr_thread_entries threads;

	const uint32_t thread_max;
	const uint32_t callers_max;

	struct fbr_allocator alloc;
};

/* This is just for initialization. I keep it here so I'm reminded to change both */
struct fbr_pool_mutable {
	void *job_queue;
	struct fbr_queue_ops job_queue_ops;
	int active;

	unsigned int thread_num;
	struct fbr_tw_ql_packed tw_ql;

	int32_t thread_kill_num;

	struct fbr_thread_entries threads;

	uint32_t thread_max;
	uint32_t callers_max;

	struct fbr_allocator alloc;
};

inline static void fbr_free_sync(struct fbr_pool *pool)
{
	void *jq;
	void (*jq_free)(void *);
	void (*alloc_free)(void *);

	fbr_assert(pool != NULL);

	jq = pool->job_queue;
	jq_free = pool->job_queue_ops.free;
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
	fbr_thread_entries_free(&pool->threads, alloc_free);

	/* Free the pool */
	alloc_free(pool);
}

inline static bool fbr_pool_active(const struct fbr_pool *pool)
{
	fbr_assert(pool != NULL);
	return ck_pr_load_int(&pool->active) != 0;
}

#endif /* _FBR_INTERNAL_H */
