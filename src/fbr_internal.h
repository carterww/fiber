/* See LICENSE file for copyright and license details. */

#ifndef _FBR_INTERNAL_H
#define _FBR_INTERNAL_H

#include <limits.h>

#include <fbr.h>

#include "fbr_cc.h"
#include "fbr_epoch.h"
#include "fbr_job.h"
#include "fbr_platform.h"
#include "fbr_thread_entries.h"
#include "fbr_wait.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

union fbr_tw_ql_packed {
	struct {
		uint32_t thread_working;
		uint32_t queue_length;
	} items FBR_ATTR_ALIGNED(8) FBR_ATTR_PACKED;
	uint64_t combined;
};

struct fbr_pool {
	void *job_queue;
	struct fbr_queue_ops job_queue_ops;
	int active;

	/* These are shared variables that change often. Each one will
	 * be padded to avoid false sharing.
	 */
	union fbr_tw_ql_packed tw_ql;
	char _pad1[FBR_CACHELINE_BYTES - sizeof(union fbr_tw_ql_packed)];
	int32_t thread_kill_num;
	char _pad2[FBR_CACHELINE_BYTES - sizeof(int32_t)];
	uint32_t thread_num;
	char _pad3[FBR_CACHELINE_BYTES - sizeof(uint32_t)];

	struct fbr_job_entries jobs_current;
	struct fbr_thread_entries threads;
	struct fbr_wait_entries waiters;
	struct fbr_epoch_entries wait_epoch;

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
	ck_pr_store_ptr(&pool->jobs_current.array, NULL);
	ck_pr_store_ptr(&pool->threads.array, NULL);
	ck_pr_store_ptr(&pool->waiters.array, NULL);
	ck_pr_store_ptr(&pool->wait_epoch.array, NULL);
	ck_pr_store_ptr(&pool->alloc.malloc, NULL);
	ck_pr_store_ptr(&pool->alloc.free, NULL);
	ck_pr_fence_memory();

	/* Free job queue */
	jq_free(jq);

	/* Free bm allocators */
	fbr_job_entries_free(&pool->jobs_current, alloc_free);
	fbr_thread_entries_free(&pool->threads, alloc_free);
	fbr_wait_entries_free(&pool->waiters, alloc_free);
	fbr_epoch_entries_free(&pool->wait_epoch, alloc_free);

	/* Free the pool */
	alloc_free(pool);
}

inline static bool fbr_pool_active(const struct fbr_pool *pool)
{
	fbr_assert(pool != NULL);
	return ck_pr_load_int(&pool->active) != 0;
}

inline static bool fbr_wait_can_wake(const struct fbr_pool *pool)
{
	uint32_t thread_num;
	uint64_t tw_ql_raw;
	union fbr_tw_ql_packed tw_ql;

	fbr_assert(pool != NULL);

	thread_num = ck_pr_load_32(&pool->thread_num);
	if (thread_num == 0) {
		return true;
	}
	tw_ql_raw = ck_pr_load_64(&pool->tw_ql.combined);
	tw_ql.combined = tw_ql_raw;
	return tw_ql.items.queue_length == 0 && tw_ql.items.thread_working == 0;
}

#endif /* _FBR_INTERNAL_H */
