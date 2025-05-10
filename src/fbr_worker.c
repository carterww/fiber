#include "ck_pr.h"

#include <fbr_errno.h>
#include <fbr_new.h>

#include "fbr_bm_alloc.h"
#include "fbr_debug.h"
#include "fbr_internal.h"
#include "fbr_packed_counters.h"
#include "fbr_thread.h"
#include "fbr_worker.h"

static bool fbr_worker_thread_entry_index_get(const struct fbr_pool *pool,
					      const tid_t *tid,
					      unsigned int *idx)
{
	const unsigned int entries_num = FBR_BM_ALLOC_CAP(&pool->threads.meta);
	struct fbr_thread *entries = ck_pr_load_ptr(&pool->threads.array);
	for (unsigned int i = 0; i < entries_num; ++i) {
		struct fbr_thread *entry = &entries[i];
		enum fbr_thread_type type = ck_pr_load_int(&entry->type);
		if (type == FBR_THREAD_TYPE_INTERNAL &&
		    fbr_thread_tid_equal(tid, &entry->thread.internal.id)) {
			*idx = i;
			return true;
		}
	}
	return false;
}

static struct fbr_thread *
fbr_worker_thread_entry_get(const struct fbr_pool *pool, const tid_t *tid)
{
	unsigned int idx;
	if (fbr_worker_thread_entry_index_get(pool, tid, &idx)) {
		struct fbr_thread *entries =
			ck_pr_load_ptr(&pool->threads.array);
		return &entries[idx];
	} else {
		return NULL;
	}
}

static void fbr_worker_cleanup(void *tls_ptr);

void *fbr_worker_runner_internal(void *pool_ptr)
{
	struct fbr_pool *pool;
	tid_t thread_id;
	unsigned int thread_idx;
	bool thread_entry_found;
	fbr_errno_t err_setup;
	struct fbr_worker_tls *tls;
	struct fbr_thread *entries;
	struct fbr_thread *entry;
	int canceled_old;

	/* Don't allow thread to be canceled during setup */
	err_setup = fbr_thread_cancel_disable();
	fbr_assert(err_setup == FBR_EOK);

	pool = (struct fbr_pool *)pool_ptr;
	fbr_assert(pool != NULL);
	fbr_assert(pool->threads.array != NULL);
	thread_id = fbr_thread_self();
	thread_entry_found = fbr_worker_thread_entry_index_get(pool, &thread_id,
							       &thread_idx);
	fbr_assert(thread_entry_found == true);
	ck_pr_add_uint(&pool->thread_num, 1);
	entries = ck_pr_load_ptr(&pool->threads.array);
	entry = &entries[thread_idx];

	tls = pool->alloc.malloc(sizeof(*tls));
	if (tls == NULL) {
		canceled_old = ck_pr_fas_int(&entry->thread.internal.canceled, 1);
		if (canceled_old == 0) {
			fbr_thread_detach(&thread_id);
		}
		struct fbr_worker_tls tls_stack = {
			pool, thread_id, thread_idx, true,
		};
		fbr_worker_cleanup(&tls_stack);
		/* Someone tried to cancel this thread but canceling was disabled.
		 * This should not be done before calling the cleanup function.
		 */
		if (canceled_old != 0) {
			fbr_thread_cancel_enable();
		}
		fbr_thread_exit(NULL);
		return NULL;
	}
	tls->pool = pool;
	tls->thread_id = thread_id;
	tls->thread_idx = thread_idx;
	tls->on_stack = false;

	fbr_thread_cleanup_push(fbr_worker_cleanup, tls);
	err_setup = fbr_thread_cancel_type_set(FBR_THREAD_CANCEL_DEFERRED);
	fbr_assert(err_setup == FBR_EOK);
	err_setup = fbr_thread_cancel_enable();
	fbr_assert(err_setup == FBR_EOK);

	fbr_worker_runner_loop(pool);

	/* Reaching this point means the thread is trying to end itself. */
	canceled_old = ck_pr_fas_int(&entry->thread.internal.canceled, 1);
	/* If canceled was 0 then nobody will or did call fbr_thread_cancel on this
	 * thread.
	 */
	if (canceled_old == 0) {
		fbr_thread_cancel_disable();
		fbr_thread_detach(&thread_id);
	}
	fbr_thread_cleanup_pop(1);
	fbr_thread_exit(NULL);
	return NULL;
}

void fbr_worker_runner_loop(struct fbr_pool *pool)
{
	struct fbr_job buff;
	unsigned int pop_num;

loop:
	while (true) {
		pop_num = pool->job_queue_ops.pop(pool->job_queue, &buff);
		if (pop_num == 0) {
			/* TODO: Sleep until we think job is available */
			continue;
		}
		fbr_assert(buff.cb != NULL);
		fbr_packed_counters_addlo_subhi(&pool->twlo_qlhi, 1, pop_num);
		/* Keep popping jobs without altering thread working count */
		while (true) {
			(void)buff.cb(buff.cb_arg);
			if (ck_pr_load_int(&pool->thread_kill_num) > 0) {
				goto handle_exit;
			}
			pop_num = pool->job_queue_ops.pop(pool->job_queue, &buff);
			if (pop_num == 0) {
				/* Go back to top to outer loop to sleep */
				break;
			}
			fbr_packed_counters_subhi(&pool->twlo_qlhi, pop_num);
		}
		fbr_packed_counters_sublo(&pool->twlo_qlhi, 1);
		if (ck_pr_load_int(&pool->thread_kill_num) > 0) {
			goto handle_exit;
		}
	}
handle_exit: {
	int to_kill = ck_pr_faa_int(&pool->thread_kill_num, -1) - 1;
	if (to_kill < 0) {
		/* Value negative. Add back to fix count and continue exeuction */
		ck_pr_faa_int(&pool->thread_kill_num, 1);
		goto loop;
	} else {
		if (to_kill > 0) {
			/* All other threads could be sleeping on the queue so we need to wake
			 * one up.
			 * TODO: Implement that.
			 */
		}
		return;
	}
}
}

static void fbr_worker_cleanup_internal(void *tls_ptr)
{
	struct fbr_worker_tls *tls = (struct fbr_worker_tls *)tls_ptr;
	fbr_assert(tls != NULL);
	struct fbr_pool *pool = tls->pool;
	fbr_assert(pool != NULL);

	fbr_bm_free(&pool->threads.meta, tls->thread_idx);
	ck_pr_fence_atomic();
	ck_pr_sub_uint(&pool->thread_num, 1);
	
	if (!tls->on_stack) {
		pool->alloc.free(tls);
	}
}
