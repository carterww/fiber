/* See LICENSE file for copyright and license details. */

#include "fbr_packed_counters.h"
#include <stdlib.h>
#include <string.h>

#include <ck_pr.h>

#include <fbr_errno.h>
#include <fbr_new.h>

#include "fbr_bm_alloc.h"
#include "fbr_debug.h"
#include "fbr_internal.h"
#include "fbr_thread.h"
#include "fbr_worker.h"

static fbr_errno_t fbr_worker_create(struct fbr_pool *pool, unsigned int num,
				     unsigned int *real_num);

static void fbr_worker_cancel(struct fbr_pool *pool, unsigned int num);

struct fbr_init_result fbr_init(const struct fbr_init_options *opt)
{
	struct fbr_init_result res = { FBR_EGENERIC, NULL };
	struct fbr_pool_mutable *pool_m = NULL;
	struct fbr_pool *pool = NULL;
	struct fbr_thread *threads = NULL;
	struct fbr_queue_init_result queue_res = { FBR_EGENERIC, NULL };
	fbr_errno_t worker_create_errno = FBR_EGENERIC;
	unsigned int worker_num = 0;

	/* Validate options */
	if (opt == NULL) {
		res.error = FBR_ENULL_ARG;
		return res;
	}
	if (opt->queue_ops.push == NULL || opt->queue_ops.pop == NULL ||
	    opt->queue_ops.init == NULL || opt->queue_ops.free == NULL) {
		res.error = FBR_ENO_QUEUE_OPS;
		return res;
	}
	if (opt->allocator.malloc == NULL || opt->allocator.free == NULL) {
		res.error = FBR_ENO_ALLOC;
		return res;
	}
	if (opt->thread_num > opt->thread_max) {
		res.error = FBR_EINVAL;
		return res;
	}
	/* Allocate the pool and set primitives */
	pool_m = opt->allocator.malloc(sizeof(*pool_m));
	if (pool_m == NULL) {
		res.error = FBR_ENOMEM;
		return res;
	}
	pool_m->job_queue = NULL;
	pool_m->job_queue_ops = opt->queue_ops;
	pool_m->job_id_counter = 1;
	pool_m->active = 1;
	pool_m->thread_num = 0;
	pool_m->twlo_qlhi.parts.lo = 0;
	pool_m->twlo_qlhi.parts.hi = 0;
	pool_m->thread_kill_num = 0;
	pool_m->thread_max = opt->thread_max;
	pool_m->caller_max = opt->callers_max;
	pool_m->alloc = opt->allocator;

	/* Initialization after this point must goto init_error in order to cleanup
	 * resources.
	 */

	/* Allocate threads bm allocator. */
	threads = fbr_bm_alloc_init(&pool_m->threads.meta,
				    sizeof(*pool_m->threads.array),
				    opt->thread_max, opt->allocator.malloc);
	if (threads == NULL) {
		res.error = FBR_ENOMEM;
		goto init_error;
	}
	for (unsigned int i = 0; i < opt->thread_max; ++i) {
		struct fbr_thread *t;
		t = &pool_m->threads.array[i];
		t->type = FBR_THREAD_TYPE_NONE;
	}

	/* Initialize job queue */
	queue_res = opt->queue_ops.init(opt->queue_len, opt->allocator);
	if (queue_res.error != FBR_EOK || queue_res.queue == NULL) {
		res.error = queue_res.error;
		goto init_error;
	}

	/* DON'T USE pool_m ANYMORE !UB!UB! */
	pool = (struct fbr_pool *)pool_m;

	/* Start threads */
	ck_pr_fence_memory(); // Make sure workers will see correct data
	worker_create_errno =
		fbr_worker_create(pool, opt->thread_num, &worker_num);
	if (worker_create_errno != FBR_EOK || opt->thread_num != worker_num) {
		res.error = worker_create_errno;
		goto init_error;
	}

	res.error = FBR_EOK;
	res.pool = (struct fbr_pool *)pool_m;
	return res;
init_error: {
	pool = (struct fbr_pool *)pool_m;
	ck_pr_fas_int(&pool->active, 0);
	ck_pr_barrier();

	if (worker_create_errno != FBR_EOK || opt->thread_num != worker_num) {
		fbr_worker_cancel((struct fbr_pool *)pool_m, opt->thread_num);
	}
	/* If any workers were spawned then the last one to exit will be responsible
	 * for cleaning up the other resources.
	 */
	if (worker_num > 0) {
		return res;
	}
	if (queue_res.error == FBR_EOK && queue_res.queue != NULL) {
		opt->queue_ops.free(queue_res.queue);
	}
	if (threads != NULL) {
		fbr_bm_alloc_free(&pool->threads.meta, opt->allocator.free);
	}
	if (pool != NULL) {
		opt->allocator.free(pool);
	}
	return res;
}
}

void fiber_free(fbr_pool_t *pool)
{
	int active;
	unsigned int thread_num;
	if (pool == NULL) {
		return;
	}
	active = ck_pr_fas_int(&pool->active, 0);
	if (!active) {
		return;
	}
	ck_pr_fence_atomic_load();
	thread_num = ck_pr_load_uint(&pool->thread_num);
	if (thread_num > 0) {
		/* There is at least one thread that will see active = false
		 * and cleanup.
		 */
		fbr_worker_cancel(pool, thread_num);
		return;
	}
	/* Cleanup here if no threads in pool */
	fbr_free_sync(pool);
}

fbr_errno_t fbr_job_push(fbr_pool_t *pool, fbr_job_t *job);

fbr_errno_t fbr_job_push_raw(fbr_pool_t *pool, const fbr_job_t *job);

fbr_errno_t fbr_wait(fbr_pool_t *pool);

fbr_errno_t fbr_wait_job(fbr_pool_t *pool, uint64_t job_id);

fbr_errno_t fbr_thread_add(fbr_pool_t *pool, unsigned int thread_num);

fbr_errno_t fbr_thread_remove(fbr_pool_t *pool, unsigned int thread_num);

unsigned int fbr_thread_working(const fbr_pool_t *pool)
{
	struct fbr_packed_counters counters;

	if (pool == NULL || !fbr_pool_active(pool)) {
		return 0;
	}
	counters = fbr_packed_counters_load(&pool->twlo_qlhi);
	return counters.lo;
}

unsigned int fbr_jobs_pending(const fbr_pool_t *pool)
{
	struct fbr_packed_counters counters;

	if (pool == NULL || !fbr_pool_active(pool)) {
		return 0;
	}
	counters = fbr_packed_counters_load(&pool->twlo_qlhi);
	return counters.hi;
}

unsigned int fbr_thread_num(const fbr_pool_t *pool)
{
	if (pool == NULL || !fbr_pool_active(pool)) {
		return 0;
	}
	return ck_pr_load_uint(&pool->thread_num);
}

unsigned int fbr_thread_max(const fbr_pool_t *pool)
{
	if (pool == NULL || !fbr_pool_active(pool)) {
		return 0;
	}
	return ck_pr_load_uint(&pool->thread_max);
}

unsigned int fbr_callers_max(const fbr_pool_t *pool)
{
	if (pool == NULL || !fbr_pool_active(pool)) {
		return 0;
	}
	return ck_pr_load_uint(&pool->callers_max);
}

static fbr_errno_t fbr_worker_create(struct fbr_pool *pool, unsigned int num,
				     unsigned int *real_num)
{
	*real_num = 0;
	for (; *real_num < num; *real_num += 1) {
		fbr_errno_t err;
		unsigned int idx;
		struct fbr_thread *thread_entries;
		struct fbr_thread *thread_entry;

		err = fbr_bm_malloc(&pool->threads.meta, &idx);
		/* The pool will not allow more than pool->thread_max to be spawned
		 * so in theory this shouldn't happen. In reality there are cases
		 * when the slots were full and and one was just freed behind the
		 * iterator.
		 *
		 * We won't retry here because the caller should make that decision.
		 */
		if (err == FBR_ENOMEM) {
			return err;
		}
		fbr_assert(err == FBR_EOK);

		thread_entries = ck_pr_load_ptr(&pool->threads.array);
		thread_entry = &thread_entries[idx];

		/* Canceled must be visible before the type */
		ck_pr_store_int(&thread_entry->thread.internal.canceled, 0);
		ck_pr_fence_store();
		ck_pr_store_int(&thread_entry->type, FBR_THREAD_TYPE_INTERNAL);
		err = fbr_thread_create(&thread_entry->thread.internal.id,
					fbr_worker_runner_internal,
					(void *)pool);
		if (err != FBR_EOK) {
			return err;
		}
		err = fbr_thread_detach(&thread_entry->thread.internal.id);
		fbr_assert(err == FBR_EOK);
	}
	return FBR_EOK;
}

static void fbr_worker_cancel(struct fbr_pool *pool, unsigned int num)
{
	unsigned int count = 0;
	unsigned int idx;
	struct fbr_bm_alloc_iterator iter;

	fbr_bm_iterator_init(&pool->threads.meta, &iter);
	while (count < num &&
	       fbr_bm_iterator_next(&pool->threads.meta, &iter, &idx)) {
		struct fbr_thread *thread_arr;
		struct fbr_thread *thread_entry;
		enum fbr_thread_type type;
		fbr_errno_t err;

		thread_arr = ck_pr_load_ptr(&pool->threads.array);
		thread_entry = &thread_arr[idx];

		type = ck_pr_load_int(&thread_entry->type);
		if (type != FBR_THREAD_TYPE_INTERNAL) {
			continue;
		}
		ck_pr_fence_load_atomic();
		int canceled_old = ck_pr_fas_int(
			&thread_entry->thread.internal.canceled, 1);
		/* Don't cancel if previous value was not 0. Someone else did or doing */
		if (canceled_old != 0) {
			continue;
		}
		err = fbr_thread_cancel(&thread_entry->thread.internal.id);
		fbr_assert(err == FBR_EOK);
		++count;
	}
}
