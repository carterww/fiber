#include <limits.h>

#include <ck_pr.h>

#include <fbr.h>
#include <fbr_errno.h>

#include "fbr_cc.h"
#include "fbr_debug.h"
#include "fbr_futex.h"
#include "fbr_internal.h"
#include "fbr_thread.h"
#include "fbr_thread_entries.h"
#include "fbr_worker.h"

enum fbr_trysleep_queue_wakeup_reason {
	FBR_TRYSLEEP_QUEUE_WAKE_JOB_AVAILABLE = 0,
	FBR_TRYSLEEP_QUEUE_WAKE_KILLING_TIME,
	FBR_TRYSLEEP_QUEUE_WAKE_POOL_INACTIVE,
};

static void fbr_worker_cleanup(void *tls_ptr);

static enum fbr_trysleep_queue_wakeup_reason
fbr_worker_trysleep_on_queue(struct fbr_pool *pool);

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
	struct fbr_job buff;
	uint32_t pop_num;

loop:
	while (true) {
		pop_num = pool->job_queue_ops.pop(pool->job_queue, &buff);
		if (pop_num == 0) {
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
		ck_pr_sub_32(&pool->tw_ql.queue_length, pop_num);
		ck_pr_fence_atomic();
		ck_pr_inc_32(&pool->tw_ql.thread_working);

		/* Keep popping jobs without altering thread working count */
		while (true) {
			(void)buff.cb(buff.cb_arg);
			if (ck_pr_load_int(&pool->thread_kill_num) > 0 ||
			    !fbr_pool_active(pool)) {
				ck_pr_dec_32(&pool->tw_ql.thread_working);
				goto handle_exit;
			}
			pop_num =
				pool->job_queue_ops.pop(pool->job_queue, &buff);
			if (pop_num == 0) {
				/* Go back to top to outer loop to sleep */
				break;
			}
			ck_pr_sub_32(&pool->tw_ql.queue_length, pop_num);
		}
		ck_pr_dec_32(&pool->tw_ql.thread_working);
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
			wake_err = fbr_futex_wake(&pool->tw_ql.queue_length,
						  &uto_kill);
			fbr_assert(wake_err == FBR_EOK);
		}
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
		err = fbr_futex_wait(&pool->tw_ql.queue_length, 0);
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
		queue_len = ck_pr_load_32(&pool->tw_ql.queue_length);
		if (queue_len != 0) {
			return FBR_TRYSLEEP_QUEUE_WAKE_JOB_AVAILABLE;
		}
	}
	fbr_unreachable();
}
