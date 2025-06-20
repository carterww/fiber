// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Carter Williams

#include <limits.h>

#include <ck_pr.h>

#include <fbr.h>
#include <fbr_errno.h>

#include "fbr_cc.h"
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

enum fbr_trysleep_queue_wakeup_reason {
	FBR_TRYSLEEP_QUEUE_WAKE_JOB_AVAILABLE = 0,
	FBR_TRYSLEEP_QUEUE_WAKE_KILLING_TIME,
	FBR_TRYSLEEP_QUEUE_WAKE_POOL_INACTIVE,
};

static void fbr_worker_cleanup(void *thread_entry_ptr);

static enum fbr_trysleep_queue_wakeup_reason
fbr_worker_trysleep_on_queue(struct fbr_pool *pool);

inline static bool fbr_waiters_can_wake_in_loop(const struct fbr_pool *pool,
						uint64_t *timestamp)
{
	uint64_t tb, te;
	uint64_t tw_ql_raw;
	union fbr_tw_ql_packed tw_ql;

	/* Keep checking condition until the timestamp doesn't change betwen checks. */
	while (1) {
		tb = ck_pr_load_64(&pool->waiters.timestamp_global);
		ck_pr_fence_load();
		tw_ql_raw = ck_pr_load_64(&pool->tw_ql.combined);
		ck_pr_fence_load();
		te = ck_pr_load_64(&pool->waiters.timestamp_global);
		if (tb != te) {
			continue;
		}
		*timestamp = tb;
		tw_ql.combined = tw_ql_raw;
		return tw_ql.items.queue_length == 0 &&
		       tw_ql.items.thread_working == 0;
	}
}

inline static bool fbr_waiters_can_wake_on_exit(const struct fbr_pool *pool,
						uint64_t *timestamp)
{
	uint64_t tb, te;
	uint32_t tnum;

	/* Keep checking condition until the timestamp doesn't change betwen checks. */
	while (1) {
		tb = ck_pr_load_64(&pool->waiters.timestamp_global);
		ck_pr_fence_load();
		tnum = ck_pr_load_32(&pool->thread_num);
		ck_pr_fence_load();
		te = ck_pr_load_64(&pool->waiters.timestamp_global);
		if (tb != te) {
			continue;
		}
		*timestamp = tb;
		return tnum == 0;
	}
}

inline static void fbr_waiters_wake_in_loop(struct fbr_pool *pool,
					    struct fbr_hp_entry *hp,
					    uint32_t retired_thresh)
{
	uint64_t timestamp;
	uint32_t retired_approx;

	bool can_wake = fbr_waiters_can_wake_in_loop(pool, &timestamp);
	if (!can_wake) {
		return;
	}
	retired_approx = fbr_wait_entries_wake(&pool->waiters, hp, timestamp);
	if (retired_approx >= retired_thresh) {
		fbr_wait_entries_reclaim(&pool->waiters, &pool->wait_hp, hp);
	}
}

inline static void fbr_waiters_wake_on_exit(struct fbr_pool *pool,
					    struct fbr_hp_entry *hp,
					    uint64_t timestamp)
{
	(void)fbr_wait_entries_wake(&pool->waiters, hp, timestamp);
	fbr_wait_entries_reclaim(&pool->waiters, &pool->wait_hp, hp);
}

inline static void fbr_waiters_job_wake(struct fbr_pool *pool,
					struct fbr_hp_entry *hp,
					uint64_t job_id,
					uint32_t retired_thresh)
{
	uint32_t retired_approx;

	retired_approx =
		fbr_wait_job_entries_wake(&pool->waiters_job, hp, job_id);
	if (retired_approx >= retired_thresh) {
		fbr_wait_job_entries_reclaim(&pool->waiters_job,
					     &pool->wait_job_hp, hp);
	}
}

void *fbr_worker_runner_internal(void *pool_ptr)
{
	uint32_t thread_idx = UINT32_MAX;
	struct fbr_pool *pool;
	tid_t thread_id;
	bool thread_entry_found;
	fbr_errno_t err_setup;
	struct fbr_thread *entry;

	err_setup = fbr_thread_cancel_disable();
	fbr_assert(err_setup == FBR_EOK);

	pool = (struct fbr_pool *)pool_ptr;
	fbr_assert(pool != NULL);
	fbr_assert(pool->threads.array != NULL);
	thread_id = fbr_thread_self();
	thread_entry_found = fbr_thread_entry_get_internal(
		&pool->threads, &thread_id, &thread_idx);
	fbr_assert(thread_entry_found == true);
	fbr_assert(thread_idx != UINT32_MAX);
	ck_pr_inc_uint(&pool->thread_num);
	entry = &pool->threads.array[thread_idx];

	fbr_thread_cleanup_push(fbr_worker_cleanup, entry);

	fbr_worker_runner_loop(pool);

	/* Reaching this point means the thread is trying to end itself. */
	fbr_thread_cleanup_pop(1);
	fbr_thread_exit(NULL);
	return NULL;
}

fbr_errno_t fbr_worker_runner_external(struct fbr_pool *pool,
				       uint64_t thread_id)
{
	uint32_t thread_idx;
	fbr_errno_t te_malloc_err;
	struct fbr_thread *thread_entry;

	fbr_assert(pool != NULL);
	fbr_assert(pool->threads.array != NULL);

	te_malloc_err = fbr_thread_entry_malloc(
		&pool->threads, FBR_THREAD_TYPE_EXTERNAL, &thread_idx);
	if (te_malloc_err == FBR_ENOMEM) {
		return FBR_ENOMEM;
	}
	fbr_assert(te_malloc_err == FBR_EOK);
	thread_entry = &pool->threads.array[thread_idx];
	thread_entry->pool = pool;
	thread_entry->thread_idx = thread_idx;
	ck_pr_store_64(&thread_entry->thread.external.id, thread_id);
	ck_pr_fence_store_atomic();
	ck_pr_inc_uint(&pool->thread_num);

	fbr_worker_runner_loop(pool);

	fbr_worker_cleanup(&thread_entry);
	return FBR_EOK;
}

void fbr_worker_runner_loop(struct fbr_pool *pool)
{
	struct fbr_job_entry *job_current = NULL;
	struct fbr_hp_entry *wait_hp = NULL;
	struct fbr_hp_entry *wait_job_hp = NULL;
	struct fbr_job buff;
	uint32_t waiters_retired_threshhold;
	uint32_t pop_num;
	const bool wait_enable = pool->wait_enable;
	const bool wait_job_enable = pool->wait_job_enable;

	fbr_errno_t job_entry_err =
		fbr_job_entry_malloc(&pool->jobs_current, &job_current);
	fbr_assert(job_entry_err == FBR_EOK);
	fbr_assert(job_current != NULL);

	if (wait_enable) {
		fbr_errno_t wait_hp_err =
			fbr_hp_malloc(&pool->wait_hp, &wait_hp);
		fbr_assert(wait_hp_err == FBR_EOK);
		fbr_assert(wait_hp != NULL);
	}

	if (wait_job_enable) {
		fbr_errno_t wait_job_hp_err =
			fbr_hp_malloc(&pool->wait_job_hp, &wait_job_hp);
		fbr_assert(wait_job_hp_err == FBR_EOK);
		fbr_assert(wait_job_hp != NULL);
	}

	waiters_retired_threshhold = MAX(
		1,
		fbr_hp_array_len(pool->thread_max, pool->callers_max, 1) / 4);
loop:
	while (true) {
		pop_num = pool->job_queue_ops.pop(pool->job_queue, &buff,
						  job_current);
		if (pop_num == 0) {
			fbr_assert(ck_pr_load_int(&job_current->active) == 0);
			/* Before possibly going to sleep check if there are any waiters */
			if (wait_enable) {
				fbr_waiters_wake_in_loop(
					pool, wait_hp,
					waiters_retired_threshhold);
			}
			switch (fbr_worker_trysleep_on_queue(pool)) {
			case FBR_TRYSLEEP_QUEUE_WAKE_JOB_AVAILABLE:
				goto loop;
			case FBR_TRYSLEEP_QUEUE_WAKE_KILLING_TIME:
				goto handle_exit;
			case FBR_TRYSLEEP_QUEUE_WAKE_POOL_INACTIVE:
				goto exit;
			default:
				fbr_unreachable();
			}
		}
		fbr_assert(buff.cb != NULL);
		fbr_assert(ck_pr_load_int(&job_current->active) != 0);
		ck_pr_sub_32(&pool->tw_ql.items.queue_length, pop_num);
		ck_pr_inc_32(&pool->tw_ql.items.thread_working);
		ck_pr_barrier();

		/* Keep popping jobs without altering thread working count */
		while (true) {
			(void)buff.cb(buff.cb_arg);
			ck_pr_barrier();
			if (wait_job_enable) {
				fbr_waiters_job_wake(
					pool, wait_job_hp, buff.id,
					waiters_retired_threshhold);
			}

			if (!fbr_pool_active(pool)) {
				ck_pr_dec_32(&pool->tw_ql.items.thread_working);
				fbr_job_entry_set_inactive(job_current);
				goto exit;
			} else if (ck_pr_load_int(&pool->thread_kill_num) > 0) {
				ck_pr_dec_32(&pool->tw_ql.items.thread_working);
				fbr_job_entry_set_inactive(job_current);
				goto handle_exit;
			}
			pop_num = pool->job_queue_ops.pop(pool->job_queue,
							  &buff, job_current);
			if (pop_num == 0) {
				break;
			}
			ck_pr_sub_32(&pool->tw_ql.items.queue_length, pop_num);
		}
		fbr_assert(pop_num == 0);
		fbr_assert(ck_pr_load_int(&job_current->active) == 0);
		ck_pr_dec_32(&pool->tw_ql.items.thread_working);
		if (!fbr_pool_active(pool)) {
			goto exit;
		} else if (ck_pr_load_int(&pool->thread_kill_num) > 0) {
			goto handle_exit;
		}
	}
handle_exit: {
	int32_t to_kill = (int32_t)ck_pr_faa_32(
		(uint32_t *)&pool->thread_kill_num, (uint32_t)-1);
	to_kill -= 1;
	if (to_kill < 0) {
		/* Value negative. Add back to fix count and continue exeuction */
		ck_pr_inc_32((uint32_t *)&pool->thread_kill_num);
		goto loop;
	} else {
		if (to_kill > 0) {
			fbr_errno_t wake_err;
			uint32_t uto_kill = (uint32_t)to_kill;
			/* All other threads could be sleeping on the queue so we need to wake
			 * at least one up. We'll just wake at most to_kill up.
			 */
			wake_err = fbr_futex_wake(
				&pool->tw_ql.items.queue_length, &uto_kill);
			fbr_assert(wake_err == FBR_EOK);
		}
		goto exit;
	}
	fbr_unreachable();
}
exit: {
	fbr_assert(job_current != NULL);

	fbr_job_entry_free(&pool->jobs_current, job_current);
	if (wait_enable) {
		fbr_assert(wait_hp != NULL);
		fbr_hp_free(&pool->wait_hp, wait_hp);
	} else {
		fbr_assert(wait_hp == NULL);
	}
	if (wait_job_enable) {
		fbr_assert(wait_job_hp != NULL);
		fbr_hp_free(&pool->wait_job_hp, wait_job_hp);
	} else {
		fbr_assert(wait_job_hp == NULL);
	}
}
}

static void fbr_worker_cleanup(void *thread_entry_ptr)
{
	struct fbr_thread *thread_entry = (struct fbr_thread *)thread_entry_ptr;
	bool last_alive;
	int active;

	fbr_assert(thread_entry != NULL);
	struct fbr_pool *pool = thread_entry->pool;
	fbr_assert(pool != NULL);

	fbr_thread_entry_free(&pool->threads, thread_entry->thread_idx);
	ck_pr_fence_atomic();
	last_alive = ck_pr_dec_uint_is_zero(&pool->thread_num);
	ck_pr_fence_atomic_load();
	active = ck_pr_load_int(&pool->active);

	/* If last thread wake up anyone waiting on pool before exitting */
	if (last_alive) {
		uint64_t timestamp;
		// The only event where this would return false is if someone
		// added a thread between the thread_num decrement and here.
		bool can_wake = fbr_waiters_can_wake_on_exit(pool, &timestamp);
		if (can_wake) {
			struct fbr_hp_entry *hp = NULL;
			fbr_errno_t hp_malloc_err;

			hp_malloc_err = fbr_hp_malloc(&pool->wait_hp, &hp);
			fbr_assert(hp != NULL);
			fbr_assert(hp_malloc_err == FBR_EOK);
			fbr_waiters_wake_on_exit(pool, hp, timestamp);
			fbr_hp_free(&pool->wait_hp, hp);
		}
	}
	/* This thread is responsible for cleaning up the pool because it is
	 * last in inactive pool.
	 */
	if (!active && last_alive) {
		fbr_free_sync(pool);
	}
}

static enum fbr_trysleep_queue_wakeup_reason
fbr_worker_trysleep_on_queue(struct fbr_pool *pool)
{
	uint32_t queue_len;
	fbr_errno_t err;

	while (true) {
		/* Check if we need to handle any important flags before
		 * sleeping.
		 */
		if (!fbr_pool_active(pool)) {
			return FBR_TRYSLEEP_QUEUE_WAKE_POOL_INACTIVE;
		}
		if (ck_pr_load_int(&pool->thread_kill_num) > 0) {
			return FBR_TRYSLEEP_QUEUE_WAKE_KILLING_TIME;
		}
		err = fbr_futex_wait(&pool->tw_ql.items.queue_length, 0);
		fbr_assert(err == FBR_EOK || err == FBR_EAGAIN);
		/* Check if we need to handle any important flags after
		 * waking up.
		 */
		if (!fbr_pool_active(pool)) {
			return FBR_TRYSLEEP_QUEUE_WAKE_POOL_INACTIVE;
		}
		if (ck_pr_load_int(&pool->thread_kill_num) > 0) {
			return FBR_TRYSLEEP_QUEUE_WAKE_KILLING_TIME;
		}
		/* Queue is not empty, don't sleep */
		if (err == FBR_EAGAIN) {
			return FBR_TRYSLEEP_QUEUE_WAKE_JOB_AVAILABLE;
		}
		/* Check the queue length condition to ensure it's not a spurious
		 * wakeup
		 */
		queue_len = ck_pr_load_32(&pool->tw_ql.items.queue_length);
		if (queue_len != 0) {
			return FBR_TRYSLEEP_QUEUE_WAKE_JOB_AVAILABLE;
		}
	}
	fbr_unreachable();
}
