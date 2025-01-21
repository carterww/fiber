/* See LICENSE file for copyright and license details. */

#include "src/atomic.h"
#include <errno.h>
#include <stdint.h>

#include "fiber.h"
#include "threading.h"
#include "thread_list.h"
#include "utils.h"
#include "worker.h"

/* Declared and defined in fiber.c */
extern jid __fiber_job_push(struct fiber_pool *pool, struct fiber_job *job,
			    uint32_t queue_flags);

static void fiber_worker_runner_cleanup(void *fiber_worker_thread_arg);
static void __fiber_worker_runner_cleanup(struct fiber_worker_thread_arg *arg);

static void fiber_worker_loop(struct fiber_pool *pool,
			      struct fiber_thread *thread);
static void fiber_worker_execute_job(struct fiber_pool *pool,
				     struct fiber_thread *thread,
				     struct fiber_job *job);

static int fiber_worker_handle_flags(struct fiber_pool *pool);
static int fiber_worker_handle_flag_kill(struct fiber_pool *pool);
static void fiber_worker_handle_flag_wait(struct fiber_pool *pool);

static void *fiber_wake_runner(void *arg);

int fiber_workers_start(struct fiber_pool *pool,
			struct fiber_thread *threads_head,
			tpsize threads_number)
{
	int error_code = 0;
	tpsize i = 0;
	struct fiber_thread *thread_curr = threads_head;
	struct fiber_worker_thread_arg *prev = NULL;

	while (i < threads_number && thread_curr != NULL) {
		struct fiber_worker_thread_arg *arg =
			pool->malloc(sizeof(*arg));
		if (arg == NULL) {
			error_code = FBR_ENO_RSC;
			goto err;
		}
		arg->pool = pool;
		arg->thread = thread_curr;
		arg->thread->job_id = FBR_EINVLD_JOB;
		arg->prev = prev;
		prev = arg;
		error_code = fiber_worker_start(arg);
		if (error_code != 0) {
			goto err;
		}
		++i;
		thread_curr = thread_curr->next;
	}

	return 0;
err:
	if (i > 0) {
		fiber_workers_cancel(threads_head, i);
	}
	while (prev != NULL) {
		struct fiber_worker_thread_arg *saved = prev->prev;
		pool->free(prev);
		prev = saved;
	}
	return error_code;
}

int fiber_worker_start(struct fiber_worker_thread_arg *arg)
{
	int error_code = 0;

	fiber_assert(arg != NULL);
	/* TODO: Ensure threading functions return fiber error. */
	error_code = fiber_thread_create(&arg->thread->thread_id,
					 fiber_worker_runner, arg);
	if (error_code != 0) {
		return error_code;
	}
	return fiber_thread_detach(&arg->thread->thread_id);
}

void *fiber_worker_runner(void *fiber_worker_thread_arg)
{
	struct fiber_worker_thread_arg *arg;
	struct fiber_pool *pool;
	struct fiber_thread *thread;

	fiber_assert(fiber_worker_thread_arg != NULL);
	arg = (struct fiber_worker_thread_arg *)fiber_worker_thread_arg;

	pool = arg->pool;
	thread = arg->thread;
	fiber_assert(pool != NULL);
	fiber_assert(thread != NULL);

	fiber_thread_cleanup_push(fiber_worker_runner_cleanup,
				  fiber_worker_thread_arg);
	(void)fiber_thread_cancel_type_set(FIBER_THREAD_CANCEL_DEFERRED);
	(void)fiber_thread_cancel_enable();

	fiber_worker_loop(pool, thread);

	/* Pop and execute fiber_worker_runner_cleanup */
	fiber_thread_cleanup_pop(1);
	fiber_thread_exit(NULL);

	return NULL;
}

void fiber_workers_cancel(struct fiber_thread *threads_head,
			  tpsize threads_number)
{
	tpsize i = 0;
	while (threads_head != NULL && i < threads_number) {
		int res = fiber_thread_cancel(&threads_head->thread_id);
		fiber_assert(res == 0);
		threads_head = threads_head->next;
		++i;
	}
}

void fiber_worker_wake_other(struct fiber_pool *pool)
{
	int res;
	static struct fiber_job wake_job = { FIBER_JID_MIN, fiber_wake_runner,
					     NULL };
	jid job_id;
	void *(*job_func)(void *arg);
	void *job_arg;

	fiber_assert(pool != NULL);
	/* Put a job onto the queue whose sole purpose is to wake up
	 * a thread and allow it to handle the flags we just set.
         */
	res = __fiber_job_push(pool, &wake_job, FIBER_QUEUE_BLOCK);
	fiber_assert(res == 0);
}

static void fiber_worker_runner_cleanup(void *fiber_worker_thread_arg)
{
	struct fiber_worker_thread_arg *arg;
	struct fiber_pool *pool;
	struct fiber_thread *thread;

	fiber_assert(fiber_worker_thread_arg != NULL);
	arg = (struct fiber_worker_thread_arg *)fiber_worker_thread_arg;

	pool = arg->pool;
	thread = arg->thread;
	fiber_assert(pool != NULL);
	fiber_assert(thread != NULL);

	__fiber_worker_runner_cleanup(arg);
}

static void __fiber_worker_runner_cleanup(struct fiber_worker_thread_arg *arg)
{
	int lock_res;
	int unlock_res;
	struct fiber_pool *pool;
	struct fiber_thread *thread;

	pool = arg->pool;
	thread = arg->thread;

	lock_res = fiber_mutex_lock(&pool->lock);
	fiber_assert(lock_res == 0);

	fiber_thread_list_remove(&pool->thread_head, thread);
	pool->free(thread);

	unlock_res = fiber_mutex_unlock(&pool->lock);
	fiber_assert(unlock_res == 0);

	(void)atomic_fetch_sub_tpsize(&pool->threads_number, 1,
				      FIBER_ATOMIC_SEQ_CST);

	pool->free(arg);
}

static void fiber_worker_loop(struct fiber_pool *pool,
			      struct fiber_thread *thread)
{
	struct fiber_job job_buffer;

	while (1) {
		int queue_pop_res;
		int should_exit;

		atomic_store_jid(&thread->job_id, FBR_EINVLD_JOB,
				 FIBER_ATOMIC_SEQ_CST);
		queue_pop_res = pool->queue_ops->pop(
			pool->job_queue, &job_buffer, FIBER_QUEUE_BLOCK);
		fiber_assert(queue_pop_res == 0);

		(void)atomic_add_fetch_tpsize(&pool->threads_working, 1,
					      FIBER_ATOMIC_SEQ_CST);
		fiber_worker_execute_job(pool, thread, &job_buffer);
		(void)atomic_sub_fetch_tpsize(&pool->threads_working, 1,
					      FIBER_ATOMIC_SEQ_CST);

		/* Before calling pop and possibly falling asleep, handle any flags
                 * from pool.
                 */
		should_exit = fiber_worker_handle_flags(pool);
		if (should_exit) {
			return;
		}
	}
}

static void fiber_worker_execute_job(struct fiber_pool *pool,
				     struct fiber_thread *thread,
				     struct fiber_job *job)
{
	do {
		uint32_t pool_flags;
		atomic_store_jid(&thread->job_id, job->job_id,
				 FIBER_ATOMIC_SEQ_CST);
		job->job_func(job->job_arg);

		pool_flags = atomic_load_uint32(&pool->pool_flags,
						FIBER_ATOMIC_SEQ_CST);
		if (pool_flags & FIBER_POOL_FLAG_KILL_N) {
			break;
		}
	} while (pool->queue_ops->pop(pool->job_queue, job,
				      FIBER_QUEUE_NO_BLOCK) == 0);
}

static int fiber_worker_handle_flags(struct fiber_pool *pool)
{
	uint32_t pool_flags =
		atomic_load_uint32(&pool->pool_flags, FIBER_ATOMIC_SEQ_CST);
	if (pool_flags & FIBER_POOL_FLAG_KILL_N) {
		int should_exit = fiber_worker_handle_flag_kill(pool);
		if (should_exit) {
			return 1;
		}
	}
	if (pool_flags & FIBER_POOL_FLAG_WAIT) {
		fiber_worker_handle_flag_wait(pool);
	}
	return 0;
}

static int fiber_worker_handle_flag_kill(struct fiber_pool *pool)
{
	/* to_kill is the result after the subtraction */
	tpsize to_kill = atomic_sub_fetch_tpsize(&pool->threads_kill_number, 1,
						 FIBER_ATOMIC_SEQ_CST);
	if (to_kill <= 0) {
		/* Disable the kill flag */
		uint32_t off = ~FIBER_POOL_FLAG_KILL_N;
		(void)atomic_and_fetch_uint32(&pool->pool_flags, off,
					      FIBER_ATOMIC_SEQ_CST);

	} else {
		/* Wake another thread so they can end exit */
		fiber_worker_wake_other(pool);
	}

	if (to_kill >= 0) {
		return 1;
	}

	/* If to_kill is negative it means we tried to kill more threads than
         * we needed. This is ok. We just need to add one back to the threads_kill_number
         * and check other flags.
         */
	(void)atomic_add_fetch_tpsize(&pool->threads_kill_number, 1,
				      FIBER_ATOMIC_SEQ_CST);
	return 0;
}

static void fiber_worker_handle_flag_wait(struct fiber_pool *pool)
{
	int res;
	tpsize threads_working;

	threads_working = atomic_load_tpsize(&pool->threads_working,
					     FIBER_ATOMIC_SEQ_CST);
	/* If there are more threads working, do not post to the sync sem */
	if (threads_working > 0) {
		return;
	}
	res = fiber_sem_post(&pool->threads_sync);
	fiber_assert(res == 0);
}

static void *fiber_wake_runner(void *arg)
{
	(void)arg;
	return NULL;
}
