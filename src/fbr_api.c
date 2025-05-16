/* See LICENSE file for copyright and license details. */

#include <stdlib.h>
#include <string.h>

#include <ck_pr.h>

#include <fbr.h>
#include <fbr_errno.h>

#include "fbr_debug.h"
#include "fbr_epoch.h"
#include "fbr_futex.h"
#include "fbr_internal.h"
#include "fbr_job.h"
#include "fbr_thread.h"
#include "fbr_thread_entries.h"
#include "fbr_wait.h"
#include "fbr_wait_job.h"
#include "fbr_worker.h"

static fbr_errno_t fbr_worker_create(struct fbr_pool *pool, uint32_t num,
				     uint32_t *real_num);

FBR_ATTR_PUBLIC
struct fbr_init_result fbr_init(const struct fbr_init_options *opt)
{
	struct fbr_init_result res = { FBR_EGENERIC, NULL };
	struct fbr_pool *pool = NULL;
	fbr_errno_t job_entry_err = FBR_EGENERIC;
	fbr_errno_t thread_entry_err = FBR_EGENERIC;
	fbr_errno_t waiters_err = FBR_EGENERIC;
	fbr_errno_t wait_epoch_err = FBR_EGENERIC;
	fbr_errno_t waiters_job_err = FBR_EGENERIC;
	fbr_errno_t wait_job_epoch_err = FBR_EGENERIC;
	struct fbr_queue_init_result queue_res = { FBR_EGENERIC, NULL };
	fbr_errno_t worker_create_errno = FBR_EGENERIC;
	uint32_t worker_num = 0;

	/* Validate options */
	if (opt == NULL) {
		res.error = FBR_ENULL_ARG;
		return res;
	}
	if (opt->queue_ops.push == NULL || opt->queue_ops.pop == NULL ||
	    opt->queue_ops.init == NULL || opt->queue_ops.free == NULL) {
		res.error = FBR_ENULL_ARG;
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
	pool = opt->allocator.malloc(sizeof(*pool));
	if (pool == NULL) {
		res.error = FBR_ENOMEM;
		return res;
	}
	pool->job_queue = NULL;
	pool->job_queue_ops = opt->queue_ops;
	pool->active = 1;
	pool->tw_ql.items.thread_working = 0;
	pool->tw_ql.items.queue_length = 0;
	pool->thread_kill_num = 0;
	pool->thread_num = 0;
	pool->thread_max = opt->thread_max;
	pool->callers_max = opt->callers_max;
	pool->alloc = opt->allocator;
	pool->wait_enable = opt->wait_enable;
	pool->wait_job_enable = opt->wait_job_enable;

	/* Initialization after this point must goto init_error in order to cleanup
	 * resources.
	 */

	/* Init current job list allocator */
	job_entry_err = fbr_job_entries_init(
		&pool->jobs_current, opt->thread_max, opt->allocator.malloc);
	if (job_entry_err != FBR_EOK) {
		res.error = job_entry_err;
		goto init_error;
	}

	/* Init threads allocator. */
	thread_entry_err = fbr_thread_entries_init(
		&pool->threads, opt->thread_max, opt->allocator.malloc);
	if (thread_entry_err != FBR_EOK) {
		res.error = thread_entry_err;
		goto init_error;
	}

	/* Init waiters allocator. These cannot be reclaimed instantly so they
	 * are overallocated.
	 */
	if (opt->wait_enable) {
		waiters_err = fbr_wait_entries_init(
			&pool->waiters,
			fbr_epoch_buffer_len(opt->thread_max, opt->callers_max),
			opt->allocator.malloc);
		if (waiters_err != FBR_EOK) {
			res.error = waiters_err;
			goto init_error;
		}

		/* Init wait epoch allocator. */
		wait_epoch_err = fbr_epoch_entries_init(
			&pool->wait_epoch, opt->thread_max + opt->callers_max,
			opt->allocator.malloc);
		if (wait_epoch_err != FBR_EOK) {
			res.error = wait_epoch_err;
			goto init_error;
		}
	}

	/* Init job waiters allocator. These cannot be reclaimed instantly so they
	 * are overallocated.
	 */
	if (opt->wait_job_enable) {
		waiters_job_err = fbr_wait_job_entries_init(
			&pool->waiters_job,
			fbr_epoch_buffer_len(opt->thread_max, opt->callers_max),
			opt->allocator.malloc);
		if (waiters_job_err != FBR_EOK) {
			res.error = waiters_job_err;
			goto init_error;
		}

		/* Init wait job epoch allocator. */
		wait_job_epoch_err = fbr_epoch_entries_init(
			&pool->wait_job_epoch,
			opt->thread_max + opt->callers_max,
			opt->allocator.malloc);
		if (wait_job_epoch_err != FBR_EOK) {
			res.error = wait_job_epoch_err;
			goto init_error;
		}
	}

	/* Initialize job queue */
	queue_res = opt->queue_ops.init(opt->queue_len, opt->allocator);
	if (queue_res.error != FBR_EOK || queue_res.queue == NULL) {
		res.error = queue_res.error;
		goto init_error;
	}
	pool->job_queue = queue_res.queue;

	/* Start threads */
	ck_pr_fence_memory(); // Make sure workers will see correct data
	worker_create_errno =
		fbr_worker_create(pool, opt->thread_num, &worker_num);
	if (worker_create_errno != FBR_EOK || opt->thread_num != worker_num) {
		res.error = worker_create_errno;
		goto init_error;
	}

	res.error = FBR_EOK;
	res.pool = pool;
	return res;
init_error: {
	(void)ck_pr_fas_int(&pool->active, 0);
	ck_pr_barrier();

	/* If any workers were spawned then the last one to exit will be responsible
	 * for cleaning up the other resources.
	 */
	if (worker_num > 0) {
		uint32_t to_wake = worker_num;
		fbr_errno_t wake_err = fbr_futex_wake(
			&pool->tw_ql.items.queue_length, &to_wake);
		fbr_assert(wake_err == FBR_EOK);
		return res;
	}
	if (queue_res.error == FBR_EOK && queue_res.queue != NULL) {
		opt->queue_ops.free(queue_res.queue);
	}
	if (opt->wait_job_enable && wait_job_epoch_err == FBR_EOK) {
		fbr_assert(pool != NULL);
		fbr_epoch_entries_free(&pool->wait_job_epoch,
				       opt->allocator.free);
	}
	if (opt->wait_job_enable && waiters_job_err == FBR_EOK) {
		fbr_assert(pool != NULL);
		fbr_wait_job_entries_free(&pool->waiters_job,
					  opt->allocator.free);
	}
	if (opt->wait_enable && wait_epoch_err == FBR_EOK) {
		fbr_assert(pool != NULL);
		fbr_epoch_entries_free(&pool->wait_epoch, opt->allocator.free);
	}
	if (opt->wait_enable && waiters_err == FBR_EOK) {
		fbr_assert(pool != NULL);
		fbr_wait_entries_free(&pool->waiters, opt->allocator.free);
	}
	if (thread_entry_err == FBR_EOK) {
		fbr_assert(pool != NULL);
		fbr_thread_entries_free(&pool->threads, opt->allocator.free);
	}
	if (job_entry_err == FBR_EOK) {
		fbr_assert(pool != NULL);
		fbr_job_entries_free(&pool->jobs_current, opt->allocator.free);
	}
	if (pool != NULL) {
		opt->allocator.free(pool);
	}
	return res;
}
}

FBR_ATTR_PUBLIC
void fbr_free(fbr_pool_t *pool)
{
	int active;
	uint32_t thread_num;

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
		fbr_futex_wake(&pool->tw_ql.items.queue_length, &thread_num);
		return;
	} else {
		/* Cleanup here if no threads in pool */
		fbr_free_sync(pool);
	}
}

FBR_ATTR_PUBLIC
fbr_errno_t fbr_job_push(fbr_pool_t *pool, const fbr_job_t *job)
{
	uint32_t push_num;
	uint32_t jq_len;
	fbr_errno_t wake_err;

	if (pool == NULL || job == NULL || job->cb == NULL) {
		return FBR_ENULL_ARG;
	}
	if (!fbr_pool_active(pool)) {
		return FBR_EINVAL;
	}
	fbr_assert(pool->job_queue != NULL);
	fbr_assert(pool->job_queue_ops.push != NULL);
	push_num = pool->job_queue_ops.push(pool->job_queue, job);
	if (push_num == 0) {
		return FBR_EQUEUE_PUSH;
	}
	jq_len = ck_pr_faa_32(&pool->tw_ql.items.queue_length, push_num) +
		 push_num;
	if (jq_len > 0) {
		wake_err = fbr_futex_wake(&pool->tw_ql.items.queue_length,
					  &jq_len);
		fbr_assert(wake_err == FBR_EOK);
	}

	return FBR_EOK;
}

FBR_ATTR_PUBLIC
fbr_errno_t fbr_wait(fbr_pool_t *pool)
{
	fbr_errno_t res = FBR_EOK;
	fbr_errno_t epoch_malloc_err;
	fbr_errno_t wait_entry_err;
	struct fbr_epoch_entry *entry;
	uint32_t *futex;

	if (pool == NULL) {
		return FBR_ENULL_ARG;
	}
	if (!pool->wait_enable) {
		return FBR_ENOTSUP;
	}
	if (!fbr_pool_active(pool)) {
		return FBR_EINVAL;
	}
	if (fbr_wait_can_wake(pool)) {
		return FBR_EOK;
	}
	epoch_malloc_err = fbr_epoch_malloc(&pool->wait_epoch, &entry);
	if (epoch_malloc_err != FBR_EOK) {
		return epoch_malloc_err;
	}
	fbr_assert(entry != NULL);
	fbr_epoch_enter(&pool->wait_epoch, entry);
	ck_pr_barrier();
	wait_entry_err = fbr_wait_entry_add(&pool->waiters, &futex);
	if (wait_entry_err != FBR_EOK) {
		res = wait_entry_err;
		goto epoch_exit;
	}

	/* Before going to sleep check condition again */
	if (fbr_wait_can_wake(pool)) {
		goto epoch_exit;
	}

	while (ck_pr_load_32(futex) == 0) {
		res = fbr_futex_wait(futex, 0);
	}

epoch_exit:
	fbr_epoch_exit(&pool->wait_epoch, entry);
	ck_pr_barrier();
	fbr_epoch_free(&pool->wait_epoch, entry);
	return res;
}

FBR_ATTR_PUBLIC
fbr_errno_t fbr_wait_job(fbr_pool_t *pool, uint64_t job_id)
{
	fbr_errno_t res = FBR_EOK;
	fbr_errno_t epoch_malloc_err;
	fbr_errno_t wait_entry_err;
	struct fbr_epoch_entry *entry;
	uint32_t *futex;
	uint32_t idx;

	if (pool == NULL) {
		return FBR_ENULL_ARG;
	}
	if (!pool->wait_job_enable) {
		return FBR_ENOTSUP;
	}
	if (!fbr_pool_active(pool)) {
		return FBR_EINVAL;
	}
	/* The order in which we check conditions is very important. Jobs
	 * move from the queue to be executed so we must check the queue
	 * for the job before checking the current jobs.
	 *
	 * An important note is that this system is built for correctness,
	 * not necessarily accuracy. For example, a job id can be in the
	 * queue and currently exeucting according to these functions to
	 * prevent the case where the job id is in neither after it has
	 * been popped from the queue.
	 */

	/* Check condition before posting intent to wait */
	if (!pool->job_queue_ops.job_in_queue(pool->job_queue, job_id) &&
	    !fbr_job_executing(&pool->jobs_current, job_id)) {
		return FBR_EOK;
	}
	epoch_malloc_err = fbr_epoch_malloc(&pool->wait_job_epoch, &entry);
	if (epoch_malloc_err != FBR_EOK) {
		return epoch_malloc_err;
	}
	fbr_assert(entry != NULL);
	fbr_epoch_enter(&pool->wait_job_epoch, entry);
	ck_pr_barrier();

	wait_entry_err = fbr_wait_job_entry_add(&pool->waiters_job, job_id,
						&futex, &idx);
	if (wait_entry_err != FBR_EOK) {
		res = wait_entry_err;
		goto epoch_exit;
	}

	/* Before going to sleep check condition again */
	if (!pool->job_queue_ops.job_in_queue(pool->job_queue, job_id) &&
	    !fbr_job_executing(&pool->jobs_current, job_id)) {
		/* Retire here because no thread will recongnize it needs to
		 * be retired.
		 */
		(void)fbr_wait_job_entry_retire(&pool->waiters_job,
						entry->epoch, idx);
		goto epoch_exit;
	}

	while (ck_pr_load_32(futex) == 0) {
		res = fbr_futex_wait(futex, 0);
	}

epoch_exit: {
	fbr_epoch_exit(&pool->wait_job_epoch, entry);
	ck_pr_barrier();
	fbr_epoch_free(&pool->wait_job_epoch, entry);
	return res;
}
}

FBR_ATTR_PUBLIC
fbr_errno_t fbr_thread_join_pool(fbr_pool_t *pool, uint64_t thread_id)
{
	if (pool == NULL) {
		return FBR_ENULL_ARG;
	}
	if (!fbr_pool_active(pool)) {
		return FBR_EINVAL;
	}
	return fbr_worker_runner_external(pool, thread_id);
}

FBR_ATTR_PUBLIC
fbr_errno_t fbr_thread_add(fbr_pool_t *pool, uint32_t *tnum)
{
	fbr_errno_t worker_create_err;
	uint32_t expected_start;

	if (pool == NULL || tnum == NULL) {
		return FBR_ENULL_ARG;
	}
	if (*tnum == 0) {
		return FBR_EOK;
	}
	if (!fbr_pool_active(pool)) {
		return FBR_EINVAL;
	}

	expected_start = *tnum;
	worker_create_err = fbr_worker_create(pool, expected_start, tnum);
	if (worker_create_err != FBR_EOK) {
		return worker_create_err;
	}
	if (expected_start != *tnum) {
		return FBR_ENO_RSC;
	}
	return FBR_EOK;
}

FBR_ATTR_PUBLIC
fbr_errno_t fbr_thread_remove(fbr_pool_t *pool, uint32_t tnum)
{
	uint32_t thread_num;

	if (pool == NULL) {
		return FBR_ENULL_ARG;
	}
	if (tnum == 0) {
		return FBR_EOK;
	}
	if (!fbr_pool_active(pool)) {
		return FBR_EINVAL;
	}
	ck_pr_add_32((uint32_t *)&pool->thread_kill_num, tnum);
	ck_pr_fence_atomic_load();
	thread_num = ck_pr_load_uint(&pool->thread_num);
	if (thread_num > 0) {
		fbr_futex_wake(&pool->tw_ql.items.queue_length, &thread_num);
	}
	return FBR_EOK;
}

FBR_ATTR_PUBLIC
uint32_t fbr_thread_working(const fbr_pool_t *pool)
{
	if (pool == NULL || !fbr_pool_active(pool)) {
		return 0;
	}
	return ck_pr_load_32(&pool->tw_ql.items.thread_working);
}

FBR_ATTR_PUBLIC
uint32_t fbr_jobs_pending(const fbr_pool_t *pool)
{
	if (pool == NULL || !fbr_pool_active(pool)) {
		return 0;
	}
	return ck_pr_load_32(&pool->tw_ql.items.queue_length);
}

FBR_ATTR_PUBLIC
uint32_t fbr_thread_num(const fbr_pool_t *pool)
{
	if (pool == NULL || !fbr_pool_active(pool)) {
		return 0;
	}
	return ck_pr_load_uint(&pool->thread_num);
}

FBR_ATTR_PUBLIC
uint32_t fbr_thread_max(const fbr_pool_t *pool)
{
	if (pool == NULL || !fbr_pool_active(pool)) {
		return 0;
	}
	return ck_pr_load_uint(&pool->thread_max);
}

FBR_ATTR_PUBLIC
uint32_t fbr_callers_max(const fbr_pool_t *pool)
{
	if (pool == NULL || !fbr_pool_active(pool)) {
		return 0;
	}
	return ck_pr_load_uint(&pool->callers_max);
}

static fbr_errno_t fbr_worker_create(struct fbr_pool *pool, uint32_t num,
				     uint32_t *real_num)
{
	*real_num = 0;
	for (; *real_num < num; *real_num += 1) {
		fbr_errno_t err;
		unsigned int idx;
		struct fbr_thread *thread_entry;

		err = fbr_thread_entry_malloc(&pool->threads,
					      FBR_THREAD_TYPE_INTERNAL, &idx);

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

		thread_entry = &pool->threads.array[idx];
		err = fbr_thread_create(&thread_entry->thread.internal.id,
					fbr_worker_runner_internal,
					(void *)pool);
		if (err != FBR_EOK) {
			return err;
		}
		int prev_started = ck_pr_fas_int(&thread_entry->started, 1);
		fbr_assert(prev_started == 0);
		ck_pr_barrier();
		err = fbr_thread_detach(&thread_entry->thread.internal.id);
		fbr_assert(err == FBR_EOK);
	}
	return FBR_EOK;
}
