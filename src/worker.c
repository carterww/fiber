/* See LICENSE file for copyright and license details. */

#include <stdint.h>

#include "atomic.h"
#include "fiber.h"
#include "fiber_internal.h"
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
static int
fiber_worker_should_handle_flag_kill(struct fiber_pool *pool,
				     enum fiber_atomic_memorder load_memorder);
static int
fiber_worker_should_handle_flag_wait(struct fiber_pool *pool,
				     enum fiber_atomic_memorder load_memorder);
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
			error_code = FBR_ENOMEM;
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
	error_code = fiber_thread_create(&arg->thread->thread_id,
					 fiber_worker_runner, arg);
	return error_code;
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

	/* Thread is already done, let it clean itself up uninterrupted. */
	fiber_thread_cancel_disable();
	/* Detach the thread if we "canceled" it this way. Reaching this point
         * indicates we canceled the thread with fiber_thread_remove. If that's
         * the case, nobody will fiber_thread_join this thread. We must do this
         * after cancelation is disabled so we don't accidentally cancel then
         * attempt to join a detached thread.
         */
	fiber_thread_detach(&arg->thread->thread_id);
	/* Pop and execute fiber_worker_runner_cleanup */
	fiber_thread_cleanup_pop(1);
	fiber_thread_exit(NULL);

	return NULL;
}

void fiber_workers_cancel(struct fiber_thread *threads_head,
			  tpsize threads_number)
{
	struct fiber_thread *curr;
	tpsize i;

	curr = threads_head;
	i = 0;
	while (curr != NULL && i < threads_number) {
		struct fiber_thread *next;
		int res;
		tid curr_tid;

		/* Canceling and joining like this is inefficient but it
                 * avoids needing to malloc an array to store thread_ids.
                 * When the thread cleans up after itself, it frees the fiber_thread
                 * struct. We need to store the thread id in order to join.
                 */
		curr_tid = curr->thread_id;
		next = curr->next;
		/* Don't access curr after this point */
		res = fiber_thread_cancel(&curr_tid);
		fiber_assert(res == 0);
		res = fiber_thread_join(&curr_tid, NULL);
		fiber_assert(res == 0);
		curr = next;
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
	res = __fiber_job_push(pool, &wake_job, FIBER_QUEUE_NO_BLOCK);
	switch (res) {
	case 0:
		break;
	/* Queue was full. If this is the case, another worker will evetually
         * wake up anyway.
         */
	case FBR_EAGAIN:
		break;
	case FBR_EPUSH_JOB:
	default:
		panic(1);
	}
}

static void fiber_worker_runner_cleanup(void *fiber_worker_thread_arg)
{
	struct fiber_worker_thread_arg *arg;

	fiber_assert(fiber_worker_thread_arg != NULL);
	arg = (struct fiber_worker_thread_arg *)fiber_worker_thread_arg;

	fiber_assert(arg->pool != NULL);
	fiber_assert(arg->thread != NULL);

	__fiber_worker_runner_cleanup(arg);
}

static void __fiber_worker_runner_cleanup(struct fiber_worker_thread_arg *arg)
{
	tpsize prev_threads_num;
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

	prev_threads_num = atomic_fetch_sub_tpsize(&pool->threads_number, 1,
						   FIBER_ATOMIC_ACQ_REL);
	fiber_assert(prev_threads_num >= 1);
	/* This is the last thread in the pool and it is about to exit. Make
         * sure no callers to fiber_wait are left hanging.
         */
	if (prev_threads_num == 1 &&
	    fiber_worker_should_handle_flag_wait(pool, FIBER_ATOMIC_ACQUIRE)) {
		fiber_worker_handle_flag_wait(pool);
	}
	pool->free(arg);
}

static void fiber_worker_loop(struct fiber_pool *pool,
			      struct fiber_thread *thread)
{
	struct fiber_job job_buffer;

	while (1) {
		int queue_pop_res;
		int should_exit;

		/* IMPORTANT: Nobody currently loads the thread's job_id and
                 * the thread struct is private so no user should be able to.
                 * Since we only store the job_id, RELAXED can be used. This may
                 * be used in the future so it may need to be changed.
                 */
		atomic_store_jid(&thread->job_id, FBR_EINVLD_JOB,
				 FIBER_ATOMIC_RELAXED);
		queue_pop_res = pool->queue_ops->pop(
			pool->job_queue, &job_buffer, FIBER_QUEUE_BLOCK);
		fiber_assert(queue_pop_res == 0);

		(void)atomic_add_fetch_tpsize(&pool->threads_working, 1,
					      FIBER_ATOMIC_ACQ_REL);
		fiber_worker_execute_job(pool, thread, &job_buffer);
		(void)atomic_sub_fetch_tpsize(&pool->threads_working, 1,
					      FIBER_ATOMIC_ACQ_REL);

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
		tpsize to_kill;
		atomic_store_jid(&thread->job_id, job->job_id,
				 FIBER_ATOMIC_RELAXED);
		job->job_func(job->job_arg);

		/* Speed is important here. I am prioritizing speed over getting the
                 * most recent value 100% of the time by using RELAXED. This will be
                 * checked later (once there are no jobs on the queue) with a stronger
                 * memory ordering.
                 */
		to_kill = atomic_load_tpsize(&pool->threads_kill_number,
					     FIBER_ATOMIC_RELAXED);
		/* The kill flag is high priority so we should check it before
                 * popping off more jobs. Speed is important here. I am prioritizing
                 * speed over getting the most recent value 100% of the time by using
                 * RELAXED. This will be checked later (once there are no jobs on the
                 * queue) with a stronger memory ordering.
                 */
		if (fiber_worker_should_handle_flag_kill(
			    pool, FIBER_ATOMIC_RELAXED)) {
			break;
		}
	} while (pool->queue_ops->pop(pool->job_queue, job,
				      FIBER_QUEUE_NO_BLOCK) == 0);
}

static int fiber_worker_handle_flags(struct fiber_pool *pool)
{
	int should_exit = 0;

	if (fiber_worker_should_handle_flag_wait(pool, FIBER_ATOMIC_ACQUIRE)) {
		fiber_worker_handle_flag_wait(pool);
	}
	if (fiber_worker_should_handle_flag_kill(pool, FIBER_ATOMIC_ACQUIRE)) {
		should_exit = should_exit ||
			      fiber_worker_handle_flag_kill(pool);
	}

	return should_exit;
}

static int
fiber_worker_should_handle_flag_kill(struct fiber_pool *pool,
				     enum fiber_atomic_memorder load_memorder)
{
	tpsize to_kill;

	to_kill = atomic_load_tpsize(&pool->threads_kill_number, load_memorder);
	return to_kill > 0;
}

static int
fiber_worker_should_handle_flag_wait(struct fiber_pool *pool,
				     enum fiber_atomic_memorder load_memorder)
{
	tpsize waiters;

	waiters = atomic_load_tpsize(&pool->fiber_wait_callers, load_memorder);
	return waiters > 0;
}

static int fiber_worker_handle_flag_kill(struct fiber_pool *pool)
{
	/* to_kill is the result after the subtraction */
	tpsize to_kill = atomic_sub_fetch_tpsize(&pool->threads_kill_number, 1,
						 FIBER_ATOMIC_ACQ_REL);
	if (to_kill > 0) {
		/* Wake another thread so they can end exit */
		fiber_worker_wake_other(pool);
	}

	if (to_kill >= 0) {
		return 1;
	} else {
		/* If to_kill is negative it means we tried to kill more threads than
                 * we needed. This is ok; we just need to add one back to the threads_kill_number.
                 */
		(void)atomic_add_fetch_tpsize(&pool->threads_kill_number, 1,
					      FIBER_ATOMIC_ACQ_REL);
	}
	return 0;
}

static void fiber_worker_handle_flag_wait(struct fiber_pool *pool)
{
	int res;
	tpsize threads_working;
	tpsize remaining_waiters;

	threads_working = atomic_load_tpsize(&pool->threads_working,
					     FIBER_ATOMIC_ACQUIRE);
	/* If there are more threads working, do not post to the sync sem */
	if (threads_working > 0) {
		return;
	}

	while ((remaining_waiters =
			atomic_sub_fetch_tpsize(&pool->fiber_wait_callers, 1,
						FIBER_ATOMIC_ACQ_REL)) >= 0) {
		res = fiber_sem_post(&pool->threads_sync);
		fiber_assert(res == 0);
	};

	/* Subtracted one more time than needed (some other thread probably
         * called this function at the same time). No worries, just add it
         * back.
         */
	if (remaining_waiters < 0) {
		(void)atomic_add_fetch_tpsize(&pool->fiber_wait_callers, 1,
					      FIBER_ATOMIC_ACQ_REL);
	}
}

static void *fiber_wake_runner(void *arg)
{
	(void)arg;
	return NULL;
}

#if defined(FIBER_BUILD_ENV_TEST)
#include "test_internal.h"
struct fiber_test_internal_worker fiber_test_internal_worker = {
	fiber_worker_runner_cleanup,
	__fiber_worker_runner_cleanup,
	fiber_worker_loop,
	fiber_worker_execute_job,
	fiber_worker_handle_flags,
	fiber_worker_should_handle_flag_kill,
	fiber_worker_should_handle_flag_wait,
	fiber_worker_handle_flag_kill,
	fiber_worker_handle_flag_wait,
	fiber_wake_runner,
};
#endif /* FIBER_BUILD_ENV_TEST */
