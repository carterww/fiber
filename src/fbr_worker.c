#include <limits.h>

#include <ck_pr.h>

#include <fbr.h>
#include <fbr_errno.h>

#include "fbr_bm_alloc.h"
#include "fbr_cc.h"
#include "fbr_debug.h"
#include "fbr_epoch.h"
#include "fbr_futex.h"
#include "fbr_internal.h"
#include "fbr_thread.h"
#include "fbr_thread_entries.h"
#include "fbr_wait.h"
#include "fbr_worker.h"

enum fbr_trysleep_queue_wakeup_reason {
	FBR_TRYSLEEP_QUEUE_WAKE_JOB_AVAILABLE = 0,
	FBR_TRYSLEEP_QUEUE_WAKE_KILLING_TIME,
	FBR_TRYSLEEP_QUEUE_WAKE_POOL_INACTIVE,
};

static void fbr_worker_cleanup(void *tls_ptr);

static enum fbr_trysleep_queue_wakeup_reason
fbr_worker_trysleep_on_queue(struct fbr_pool *pool);

static void fbr_check_and_handle_waiters(struct fbr_pool *pool,
					 struct fbr_epoch_entry *epoch_entry,
					 uint64_t timestamp,
					 uint32_t retired_threshhold);

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

void *fbr_worker_runner_internal(void *pool_ptr)
{
	struct fbr_pool *pool;
	tid_t thread_id;
	uint32_t thread_idx;
	bool thread_entry_found;
	fbr_errno_t err_setup;
	struct fbr_worker_tls *tls;
	struct fbr_thread *entries;
	struct fbr_thread *entry;

	/* Don't allow thread to be canceled during setup */
	err_setup = fbr_thread_cancel_disable();
	fbr_assert(err_setup == FBR_EOK);

	pool = (struct fbr_pool *)pool_ptr;
	fbr_assert(pool != NULL);
	fbr_assert(pool->threads.array != NULL);
	thread_id = fbr_thread_self();
	thread_entry_found = fbr_thread_entry_get_internal(
		&pool->threads, &thread_id, &thread_idx);
	fbr_assert(thread_entry_found == true);
	ck_pr_inc_uint(&pool->thread_num);
	entries = ck_pr_load_ptr(&pool->threads.array);
	entry = &entries[thread_idx];

	/* A race is possible if we don't wait for this to be
	 * true.
	 */
	while (ck_pr_load_int(&entry->started) == 0)
		;

	/* I'd rather put this on the stack but that may lead to undefined behavior
	 * because it is the param of a fbr_thread_cleanup_pop function
	 */
	tls = pool->alloc.malloc(sizeof(*tls));
	if (tls == NULL) {
		struct fbr_worker_tls tls_stack = {
			pool,	       FBR_THREAD_TYPE_INTERNAL,
			{ thread_id }, thread_idx,
			true,
		};
		fbr_worker_cleanup(&tls_stack);
		fbr_thread_exit(NULL);
		return NULL;
	}
	tls->pool = pool;
	tls->thread_type = FBR_THREAD_TYPE_INTERNAL;
	tls->tid.thread_id_int = thread_id;
	tls->thread_idx = thread_idx;
	tls->on_stack = false;

	fbr_thread_cleanup_push(fbr_worker_cleanup, tls);
	err_setup = fbr_thread_cancel_type_set(FBR_THREAD_CANCEL_DEFERRED);
	fbr_assert(err_setup == FBR_EOK);
	err_setup = fbr_thread_cancel_enable();
	fbr_assert(err_setup == FBR_EOK);

	fbr_worker_runner_loop(pool);

	/* Reaching this point means the thread is trying to end itself. */
	fbr_thread_cancel_disable();
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
	struct fbr_worker_tls tls;

	fbr_assert(pool != NULL);
	fbr_assert(pool->threads.array != NULL);

	te_malloc_err = fbr_thread_entry_malloc(
		&pool->threads, FBR_THREAD_TYPE_EXTERNAL, &thread_idx);
	if (te_malloc_err == FBR_ENOMEM) {
		return FBR_ENOMEM;
	}
	fbr_assert(te_malloc_err == FBR_EOK);
	thread_entry = &pool->threads.array[thread_idx];

	ck_pr_store_64(&thread_entry->thread.external.id, thread_id);
	ck_pr_fence_store_atomic();
	(void)ck_pr_fas_int(&thread_entry->started, 1);
	ck_pr_inc_uint(&pool->thread_num);

	fbr_worker_runner_loop(pool);

	tls = (struct fbr_worker_tls){
		pool, FBR_THREAD_TYPE_EXTERNAL, { thread_id }, thread_idx, true
	};
	fbr_worker_cleanup(&tls);
	return FBR_EOK;
}

void fbr_worker_runner_loop(struct fbr_pool *pool)
{
	struct fbr_epoch_entry *wait_entry;
	struct fbr_job buff;
	uint32_t waiters_retired_threshhold;
	uint32_t pop_num;

	fbr_errno_t wait_epoch_err =
		fbr_epoch_malloc(&pool->wait_epoch, &wait_entry);
	fbr_assert(wait_epoch_err == FBR_EOK);
	fbr_assert(wait_entry != NULL);
	// waiters_retired_threshhold = pool->callers_max / 2;
	waiters_retired_threshhold = 1;
loop:
	while (true) {
		pop_num = pool->job_queue_ops.pop(pool->job_queue, &buff);
		if (pop_num == 0) {
			uint64_t timestamp;
			timestamp = ck_pr_load_64(&pool->waiters.timestamp_global);
			/* Before possibly going to sleep check if there are any waiters */
			// bool can_wake =
			// 	fbr_waiters_can_wake_in_loop(pool, &timestamp);
			// if (can_wake) {
				fbr_check_and_handle_waiters(
					pool, wait_entry, timestamp,
					waiters_retired_threshhold);
			// }
			switch (fbr_worker_trysleep_on_queue(pool)) {
			case FBR_TRYSLEEP_QUEUE_WAKE_JOB_AVAILABLE:
				goto loop;
			case FBR_TRYSLEEP_QUEUE_WAKE_KILLING_TIME:
				goto handle_exit;
			case FBR_TRYSLEEP_QUEUE_WAKE_POOL_INACTIVE:
				return;
			default:
				fbr_unreachable();
			}
		}
		fbr_assert(buff.cb != NULL);
		ck_pr_sub_32(&pool->tw_ql.items.queue_length, pop_num);
		ck_pr_fence_atomic();
		ck_pr_inc_32(&pool->tw_ql.items.thread_working);

		/* Keep popping jobs without altering thread working count */
		while (true) {
			(void)buff.cb(buff.cb_arg);
			if (ck_pr_load_int(&pool->thread_kill_num) > 0 ||
			    !fbr_pool_active(pool)) {
				ck_pr_dec_32(&pool->tw_ql.items.thread_working);
				goto handle_exit;
			}
			pop_num =
				pool->job_queue_ops.pop(pool->job_queue, &buff);
			if (pop_num == 0) {
				/* Go back to top to outer loop to sleep */
				break;
			}
			ck_pr_sub_32(&pool->tw_ql.items.queue_length, pop_num);
		}
		ck_pr_dec_32(&pool->tw_ql.items.thread_working);
		ck_pr_fence_atomic_load();
		if (ck_pr_load_int(&pool->thread_kill_num) > 0 ||
		    !fbr_pool_active(pool)) {
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
		fbr_epoch_free(&pool->wait_epoch, wait_entry);
		return;
	}
	fbr_unreachable();
}
}

static void fbr_worker_cleanup(void *tls_ptr)
{
	struct fbr_worker_tls *tls = (struct fbr_worker_tls *)tls_ptr;
	bool last_alive;
	int active;

	fbr_assert(tls != NULL);
	struct fbr_pool *pool = tls->pool;
	fbr_assert(pool != NULL);

	fbr_thread_entry_free(&pool->threads, tls->thread_idx);
	ck_pr_fence_atomic();
	last_alive = ck_pr_dec_uint_is_zero(&pool->thread_num);
	ck_pr_fence_atomic_load();
	active = ck_pr_load_int(&pool->active);

	/* Pool is possibly going to be freed so don't use these. */
	tls->pool = NULL;
	tls->thread_idx = UINT_MAX;

	if (!tls->on_stack) {
		pool->alloc.free(tls);
	}
	/* If last thread wake up anyone waiting on pool before exitting */
	if (last_alive) {
		uint64_t timestamp;
		// The only event where this would return false is if someone
		// added a thread between the thread_num decrement and here.
		bool can_wake = fbr_waiters_can_wake_on_exit(pool, &timestamp);
		if (can_wake) {
			struct fbr_epoch_entry *epoch_entry;
			fbr_errno_t epoch_malloc_err;
			epoch_malloc_err = fbr_epoch_malloc(&pool->wait_epoch,
							    &epoch_entry);
			fbr_assert(epoch_malloc_err == FBR_EOK);
			fbr_check_and_handle_waiters(pool, epoch_entry,
						     timestamp, 1);
			fbr_epoch_free(&pool->wait_epoch, epoch_entry);
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

static void fbr_check_and_handle_waiters(struct fbr_pool *pool,
					 struct fbr_epoch_entry *epoch_entry,
					 uint64_t timestamp,
					 uint32_t retired_threshhold)
{
	uint64_t current_epoch;
	struct fbr_bm_alloc_iterator iter;
	uint32_t idx;
	uint32_t retired_approx;

	fbr_assert(pool != NULL);
	fbr_assert(epoch_entry != NULL);

	fbr_epoch_enter(&pool->wait_epoch, epoch_entry);
	ck_pr_barrier();

	// Atomic load not necessary, nobody can change this rn
	current_epoch = epoch_entry->epoch;
	retired_approx = 0;
	fbr_bm_iterator_init(&pool->waiters.meta, &iter);
	while (fbr_bm_iterator_next(&pool->waiters.meta, &iter, &idx)) {
		int istatus;
		enum fbr_wait_entry_status status;
		uint64_t entry_timestamp;
		uint32_t nwake = 1;
		struct fbr_wait_entry *wait_entry = &pool->waiters.array[idx];
		istatus = ck_pr_load_int((int *)&wait_entry->status);
		status = (enum fbr_wait_entry_status)istatus;
		if (status != FBR_WAIT_ENTRY_ACTIVE) {
			continue;
		}
		entry_timestamp = ck_pr_load_64(&wait_entry->timestamp);
		if (fbr_timestamp_cmp(entry_timestamp, timestamp) >= 0) {
			continue;
		}
		(void)ck_pr_fas_32(&wait_entry->futex, 1);
		ck_pr_barrier();
		fbr_errno_t err = fbr_futex_wake(&wait_entry->futex, &nwake);
		fbr_assert(err == FBR_EOK);
		retired_approx = fbr_wait_entry_retire(&pool->waiters,
						       current_epoch, idx);
	}

	/* Try to free some retired nodes */
	if (retired_approx >= retired_threshhold) {
		uint64_t epoch_global =
			ck_pr_load_64(&pool->wait_epoch.epoch_global);
		fbr_wait_entries_reclaim(&pool->waiters, epoch_global,
					 pool->alloc.free);
	}

	ck_pr_barrier();
	fbr_epoch_exit(&pool->wait_epoch, epoch_entry);
}
