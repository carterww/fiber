// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Carter Williams

#include <stdlib.h>
#include <string.h>

#include <ck_pr.h>

#include <fbr.h>
#include <fbr_errno.h>

#include "fbr_debug.h"
#include "fbr_futex.h"
#include "fbr_hp.h"
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
size_t fbr_buffer_size_min(const fbr_init_options_t *opt)
{
	uint32_t hp_entries = opt->thread_max + opt->callers_max;
	uint32_t wait_entries =
		fbr_hp_array_len(opt->thread_max, opt->callers_max, 1);
	size_t wait_size = 0;
	size_t wait_job_size = 0;
	size_t base_size;
	size_t job_queue_size;

	if (opt->queue_ops.size_required == NULL) {
		return 0;
	}

	base_size = fbr_pool_size() + fbr_job_entries_size(opt->thread_max) +
		    fbr_thread_entries_size(opt->thread_max);

	if (opt->wait_enable) {
		wait_size = fbr_wait_entries_size(wait_entries, hp_entries) +
			    fbr_hp_entries_size(hp_entries);
	}

	if (opt->wait_job_enable) {
		wait_job_size =
			fbr_wait_job_entries_size(wait_entries, hp_entries) +
			fbr_hp_entries_size(hp_entries);
	}

	job_queue_size = opt->queue_ops.size_required(opt->queue_len);

	return base_size + wait_size + wait_job_size + job_queue_size;
}

FBR_ATTR_PUBLIC
struct fbr_init_result fbr_init(const struct fbr_init_options *opt,
				void *buffer, size_t buffer_size)
{
	struct fbr_init_result res = { FBR_EGENERIC, NULL };
	struct fbr_pool *pool = NULL;
	struct fbr_queue_init_result queue_res = { FBR_EGENERIC, NULL };
	fbr_errno_t worker_create_errno = FBR_EGENERIC;
	uint32_t worker_num = 0;
	bool owns_buffer = false;
	uint32_t hp_entries;
	uint32_t wait_entries;
	size_t min_buffer_size;
	size_t pool_size;
	size_t job_entries_size;
	size_t thread_entries_size;
	size_t wait_entries_size;
	size_t wait_entries_hp_size;
	size_t wait_job_entries_size;
	size_t wait_job_entries_hp_size;
	size_t job_queue_size;
	uintptr_t buffer_current;

	/* Validate options */
	if (opt == NULL) {
		res.error = FBR_ENULL_ARG;
		return res;
	}
	if (opt->queue_ops.push == NULL || opt->queue_ops.pop == NULL ||
	    opt->queue_ops.init == NULL || opt->queue_ops.free == NULL ||
	    opt->queue_ops.job_in_queue == NULL ||
	    opt->queue_ops.size_required == NULL) {
		res.error = FBR_ENULL_ARG;
		return res;
	}
	if (opt->thread_max == 0 || opt->callers_max == 0 ||
	    opt->thread_num > opt->thread_max) {
		res.error = FBR_EINVAL;
		return res;
	}
	hp_entries = opt->thread_max + opt->callers_max;
	wait_entries = fbr_hp_array_len(opt->thread_max, opt->callers_max, 1);
	pool_size = fbr_pool_size();
	job_entries_size = fbr_job_entries_size(opt->thread_max);
	thread_entries_size = fbr_thread_entries_size(opt->thread_max);
	wait_entries_size = fbr_wait_entries_size(wait_entries, hp_entries);
	wait_entries_hp_size = fbr_hp_entries_size(hp_entries);
	wait_job_entries_size =
		fbr_wait_job_entries_size(wait_entries, hp_entries);
	wait_job_entries_hp_size = fbr_hp_entries_size(hp_entries);
	job_queue_size = opt->queue_ops.size_required(opt->queue_len);

	min_buffer_size = pool_size + job_entries_size + thread_entries_size +
			  job_queue_size;
	if (opt->wait_enable) {
		min_buffer_size += wait_entries_size + wait_entries_hp_size;
	}
	if (opt->wait_job_enable) {
		min_buffer_size +=
			wait_job_entries_size + wait_job_entries_hp_size;
	}
	if (buffer != NULL && buffer_size < min_buffer_size) {
		res.error = FBR_EINVLD_SIZE;
		return res;
	}
	if (buffer == NULL) {
		if (opt->allocator.malloc == NULL ||
		    opt->allocator.free == NULL) {
			res.error = FBR_ENULL_ARG;
			return res;
		}
		buffer = opt->allocator.malloc(min_buffer_size);
		if (buffer == NULL) {
			res.error = FBR_ENOMEM;
			return res;
		}
		owns_buffer = true;
	}
	if (!fbr_aligned(buffer, fbr_alignof(*pool))) {
		if (owns_buffer) {
			opt->allocator.free(buffer);
		}
		res.error = FBR_EINVAL;
		return res;
	}
	pool = buffer;
	buffer_current = (uintptr_t)buffer + pool_size;
	pool->job_queue = NULL;
	pool->job_queue_ops = opt->queue_ops;
	pool->active = 1;
	pool->tw_ql.items.thread_working = 0;
	pool->tw_ql.items.queue_length = 0;
	pool->thread_kill_num = 0;
	pool->thread_num = 0;
	pool->thread_spawning_num = opt->thread_num;
	pool->thread_stack_size = opt->thread_stack_size_bytes;
	pool->thread_max = opt->thread_max;
	pool->callers_max = opt->callers_max;
	pool->free = opt->allocator.free;
	pool->free_futex = 0;
	pool->wait_enable = opt->wait_enable;
	pool->wait_job_enable = opt->wait_job_enable;
	pool->owns_buffer = owns_buffer;

	/* Initialization after this point must goto init_error in order to cleanup
	 * resources.
	 */

	/* Init current job list allocator */
	fbr_job_entries_init(&pool->jobs_current, opt->thread_max,
			     (void *)buffer_current, job_entries_size);
	buffer_current += job_entries_size;

	/* Init threads allocator. */
	fbr_thread_entries_init(&pool->threads, opt->thread_max,
				(void *)buffer_current, thread_entries_size);
	buffer_current += thread_entries_size;

	/* Init waiters allocator. These cannot be reclaimed instantly so they
	 * are overallocated.
	 */
	if (opt->wait_enable) {
		fbr_wait_entries_init(&pool->waiters, wait_entries, hp_entries,
				      (void *)buffer_current,
				      wait_entries_size);
		buffer_current += wait_entries_size;

		/* Init wait hp allocator. */
		fbr_hp_entries_init(&pool->wait_hp, hp_entries,
				    (void *)buffer_current,
				    wait_entries_hp_size);
		buffer_current += wait_entries_hp_size;
	}

	/* Init job waiters allocator. These cannot be reclaimed instantly so they
	 * are overallocated.
	 */
	if (opt->wait_job_enable) {
		fbr_wait_job_entries_init(&pool->waiters_job, wait_entries,
					  hp_entries, (void *)buffer_current,
					  wait_job_entries_size);
		buffer_current += wait_job_entries_size;

		/* Init wait job epoch allocator. */
		fbr_hp_entries_init(&pool->wait_job_hp, hp_entries,
				    (void *)buffer_current,
				    wait_job_entries_hp_size);
		buffer_current += wait_job_entries_hp_size;
	}

	/* Initialize job queue */
	queue_res = opt->queue_ops.init(opt->queue_len, (void *)buffer_current,
					job_queue_size, opt->allocator);
	if (queue_res.error != FBR_EOK || queue_res.queue == NULL) {
		res.error = queue_res.error;
		goto init_error;
	}
	pool->job_queue = queue_res.queue;
	buffer_current += job_queue_size;

	/* Start threads */
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

	if (ck_pr_load_32(&pool->thread_spawning_num) > 0) {
		uint32_t spawn_failed_num = opt->thread_num - worker_num;
		ck_pr_sub_32(&pool->thread_spawning_num, spawn_failed_num);
	}

	/* If any workers were spawned then the last one to exit will be responsible
	 * for cleaning up the other resources.
	 */
	if (worker_num > 0) {
		uint32_t to_wake = worker_num;
		fbr_errno_t wait_err = FBR_EOK;
		fbr_errno_t wake_err = fbr_futex_wake(
			&pool->tw_ql.items.queue_length, &to_wake);
		fbr_assert(wake_err == FBR_EOK);
		while (ck_pr_load_32(&pool->free_futex) == 0) {
			wait_err = fbr_futex_wait(&pool->free_futex, 0);
		}
		fbr_assert(wait_err == FBR_EOK);
		if (owns_buffer) {
			opt->allocator.free(buffer);
		}
		return res;
	}
	if (queue_res.error == FBR_EOK && queue_res.queue != NULL) {
		opt->queue_ops.free(queue_res.queue);
	}
	if (pool != NULL && owns_buffer) {
		opt->allocator.free(buffer);
	}
	return res;
}
}

FBR_ATTR_PUBLIC
void fbr_free(fbr_pool_t *pool)
{
	int active;
	uint32_t thr_num;
	uint32_t thr_sp_num;
	fbr_errno_t wait_err = FBR_EOK;

	if (pool == NULL) {
		return;
	}
	active = ck_pr_fas_int(&pool->active, 0);
	if (!active) {
		return;
	}
	ck_pr_fence_atomic_load();
	while ((thr_sp_num = ck_pr_load_32(&pool->thread_spawning_num)) != 0) {
		wait_err =
			fbr_futex_wait(&pool->thread_spawning_num, thr_sp_num);
	}
	fbr_assert(wait_err == FBR_EOK || wait_err == FBR_EAGAIN);
	ck_pr_fence_load();
	thr_num = ck_pr_load_32(&pool->thread_num);
	if (thr_num > 0) {
		thr_num = INT_MAX;
		fbr_futex_wake(&pool->tw_ql.items.queue_length, &thr_num);
		while (ck_pr_load_32(&pool->free_futex) == 0) {
			wait_err = fbr_futex_wait(&pool->free_futex, 0);
		}
		fbr_assert(wait_err == FBR_EOK || wait_err == FBR_EAGAIN);
	} else {
		/* Cleanup here if no threads in pool */
		fbr_free_sync(pool);
	}
	if (pool->owns_buffer) {
		pool->free(pool);
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
	fbr_errno_t hp_malloc_err;
	fbr_errno_t wait_entry_err;
	struct fbr_hp_entry *hp;
	struct fbr_wait_entry *wait_entry;

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
	hp_malloc_err = fbr_hp_malloc(&pool->wait_hp, &hp);
	if (hp_malloc_err != FBR_EOK) {
		/* TODO: Scan retired to see if any can be reclaimed */
		return hp_malloc_err;
	}
	fbr_assert(hp != NULL);
	ck_pr_barrier();
	wait_entry_err = fbr_wait_entry_add(&pool->waiters, hp, &wait_entry);
	if (wait_entry_err != FBR_EOK) {
		res = wait_entry_err;
		goto exit;
	}
	fbr_assert(wait_entry != NULL);

	ck_pr_barrier();
	/* Before going to sleep check condition again */
	if (fbr_wait_can_wake(pool)) {
		goto exit;
	}

	while (ck_pr_load_32(&wait_entry->futex) == 0) {
		res = fbr_futex_wait(&wait_entry->futex, 0);
	}
	if (res == FBR_EAGAIN) {
		res = FBR_EOK;
	}

exit:
	fbr_hp_free(&pool->wait_hp, hp);
	return res;
}

FBR_ATTR_PUBLIC
fbr_errno_t fbr_wait_job(fbr_pool_t *pool, uint64_t job_id)
{
	uint32_t retired_approx = 0;
	fbr_errno_t res = FBR_EOK;
	fbr_errno_t hp_malloc_err;
	fbr_errno_t wait_job_entry_err;
	struct fbr_hp_entry *hp;
	struct fbr_wait_job_entry *wait_job_entry;

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
	hp_malloc_err = fbr_hp_malloc(&pool->wait_job_hp, &hp);
	if (hp_malloc_err != FBR_EOK) {
		/* TODO: Scan retired to see if any can be reclaimed */
		return hp_malloc_err;
	}
	fbr_assert(hp != NULL);
	ck_pr_barrier();

	wait_job_entry_err = fbr_wait_job_entry_add(&pool->waiters_job, hp,
						    job_id, &wait_job_entry);
	if (wait_job_entry_err != FBR_EOK) {
		res = wait_job_entry_err;
		goto exit;
	}
	fbr_assert(wait_job_entry != NULL);

	ck_pr_barrier();
	/* Before going to sleep check condition again */
	if (!pool->job_queue_ops.job_in_queue(pool->job_queue, job_id) &&
	    !fbr_job_executing(&pool->jobs_current, job_id)) {
		/* Retire here because no thread will recongnize it needs to
		 * be retired.
		 */
		retired_approx = fbr_wait_job_entry_retire(&pool->waiters_job,
							   wait_job_entry);
		goto exit;
	}

	while (ck_pr_load_32(&wait_job_entry->futex) == 0) {
		res = fbr_futex_wait(&wait_job_entry->futex, 0);
	}
	if (res == FBR_EAGAIN) {
		res = FBR_EOK;
	}

exit: {
	uint32_t thresh = MAX(
		1,
		fbr_hp_array_len(pool->thread_max, pool->callers_max, 1) / 4);
	/* Cases are possible where no thread ever reclaims retired nodes retired
	 * above. To prevent that, we must check here.
	 */
	if (retired_approx >= thresh) {
		fbr_wait_job_entries_reclaim(&pool->waiters_job,
					     &pool->wait_job_hp, hp);
	}
	fbr_hp_free(&pool->wait_job_hp, hp);
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
	ck_pr_inc_32(&pool->thread_spawning_num);
	ck_pr_fence_atomic_load();
	if (!fbr_pool_active(pool)) {
		ck_pr_dec_32(&pool->thread_spawning_num);
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
	ck_pr_add_32(&pool->thread_spawning_num, *tnum);
	ck_pr_fence_atomic_load();
	if (!fbr_pool_active(pool)) {
		ck_pr_sub_32(&pool->thread_spawning_num, *tnum);
		return FBR_EINVAL;
	}

	expected_start = *tnum;
	worker_create_err = fbr_worker_create(pool, expected_start, tnum);
	if (worker_create_err != FBR_EOK) {
		return worker_create_err;
	}
	if (expected_start != *tnum) {
		uint32_t spawn_failed_num = expected_start - *tnum;
		ck_pr_sub_32(&pool->thread_spawning_num, spawn_failed_num);
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
	return ck_pr_load_32(&pool->thread_num);
}

FBR_ATTR_PUBLIC
uint32_t fbr_thread_max(const fbr_pool_t *pool)
{
	if (pool == NULL || !fbr_pool_active(pool)) {
		return 0;
	}
	return ck_pr_load_32(&pool->thread_max);
}

FBR_ATTR_PUBLIC
uint32_t fbr_callers_max(const fbr_pool_t *pool)
{
	if (pool == NULL || !fbr_pool_active(pool)) {
		return 0;
	}
	return ck_pr_load_32(&pool->callers_max);
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
		 * when the slots were full and an entry was just freed behind the
		 * iterator.
		 *
		 * We won't retry here because the caller should make that decision.
		 */
		if (err == FBR_ENOMEM) {
			return err;
		}
		fbr_assert(err == FBR_EOK);

		thread_entry = &pool->threads.array[idx];
		thread_entry->pool = pool;
		thread_entry->thread_idx = idx;
		ck_pr_fence_memory();
		err = fbr_thread_create(&thread_entry->thread.internal.id,
					fbr_worker_runner_internal,
					(void *)pool, pool->thread_stack_size);
		if (err != FBR_EOK) {
			return err;
		}
	}
	return FBR_EOK;
}
