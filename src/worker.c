/* See LICENSE file for copyright and license details. */

#include "fiber/fiber.h"
#include "fiber_internal.h"
#include "fiber_atomic/atomic.h"
#include "threading.h"
#include "thread_list.h"
#include "utils.h"
#include "worker.h"

static void fiber_worker_runner_cleanup(void *fiber_worker_thread_arg);
static void __fiber_worker_runner_cleanup(struct fiber_worker_thread_arg *arg);

static void fiber_worker_loop(struct fiber_pool *pool,
			      struct fiber_thread *thread);
static void fiber_worker_execute_job(struct fiber_pool *pool,
				     struct fiber_thread *thread,
				     struct fiber_job *job);

static int
fiber_worker_should_handle_flag_kill(const struct fiber_pool *pool,
				     enum fiber_atomic_memorder load_memorder);
static int
fiber_worker_should_handle_flag_wait(const struct fiber_pool *pool,
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
		arg->prev = prev;
		prev = arg;
		error_code = fiber_thread_create(&arg->thread->thread_id,
						 fiber_worker_runner, arg);
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

	/* I cannot get pthread_cleanup_push to work with this warning using gcc.
         * I'd like the warning for other parts of the code, so I'm going to
         * disable it for this one line. It has to do with some __builtin_expect
         * check.
         */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtraditional-conversion"
#endif /* __GNUC__ && !__clang__ */
	fiber_thread_cleanup_push(fiber_worker_runner_cleanup,
				  fiber_worker_thread_arg);
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif /* __GNUC__ && !__clang__ */
	(void)fiber_thread_cancel_type_set(FIBER_THREAD_CANCEL_DEFERRED);
	(void)fiber_thread_cancel_enable();
	(void)fiber_atomic_inc_fetch(&pool->threads_number,
				     FIBER_ATOMIC_ACQ_REL);

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

void fiber_workers_cancel(const struct fiber_pool *pool,
			  const struct fiber_thread *threads_head,
			  tpsize threads_number)
{
	const struct fiber_thread *curr;
	tid *tid_list;
	tpsize count;
	tpsize i;

	fiber_assert(threads_number > 0);

	curr = threads_head;
	/* threads_number is a limit, not an exact number. */
	for (count = 0; count < threads_number && curr != NULL; ++count) {
		curr = curr->next;
	}
	/* Attempt to malloc memory to cache the thread ids. After canceling
         * the threads the current fiber_thread struct cannot be used because the
         * thread may free it at any time.
         *
         * This is much faster, but I don't like the idea of allocating memory
         * in fiber_free. That seems like odd behavior for a library, even if
         * it is in the name of performance.
         */
	tid_list = pool->malloc((unsigned long)count * sizeof(*tid_list));
	curr = threads_head;
	/* If malloc fails here we do it the slower way */
	if (tid_list == NULL) {
		for (i = 0; i < count; ++i) {
			int res;
			tid tmp;

			tmp = curr->thread_id;
			curr = curr->next;
			res = fiber_thread_cancel(&tmp);
			fiber_assert(res == 0);
			res = fiber_thread_join(&tmp, NULL);
			fiber_assert(res == 0);
		}
		return;
	} else {
		/* With 32 threads this method was about 4x faster */
		for (i = 0; i < count; ++i) {
			int res;

			tid_list[i] = curr->thread_id;
			curr = curr->next;
			res = fiber_thread_cancel(&tid_list[i]);
			fiber_assert(res == 0);
		}
		for (i = 0; i < count; ++i) {
			int res = fiber_thread_join(&tid_list[i], NULL);
			fiber_assert(res == 0);
		}
		pool->free(tid_list);
	}
}

void fiber_worker_wake_other(const struct fiber_pool *pool)
{
	jid res;
	static struct fiber_job wake_job = { FIBER_JID_MIN, fiber_wake_runner,
					     NULL };
	void *(*job_func)(void *arg);
	void *job_arg;

	fiber_assert(pool != NULL);
	/* Put a job onto the queue whose sole purpose is to wake up
	 * a thread and allow it to handle the flags we just set.
         */
	res = fiber_job_push_raw(pool, &wake_job, FIBER_QUEUE_NO_BLOCK);
	switch (res) {
	case 0:
		break;
	/* Queue was full. If this is the case, another worker will eventually
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

	unlock_res = fiber_mutex_unlock(&pool->lock);
	fiber_assert(unlock_res == 0);

	pool->free(thread);

	prev_threads_num = fiber_atomic_fetch_dec(&pool->threads_number,
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

		queue_pop_res = pool->queue_ops.pop(
			pool->job_queue, &job_buffer, FIBER_QUEUE_BLOCK);
		fiber_assert(queue_pop_res == 0);

		(void)fiber_atomic_inc_fetch(&pool->threads_working,
					     FIBER_ATOMIC_ACQ_REL);
		fiber_worker_execute_job(pool, thread, &job_buffer);
		(void)fiber_atomic_dec_fetch(&pool->threads_working,
					     FIBER_ATOMIC_ACQ_REL);

		/* Before calling pop and possibly falling asleep, handle any flags
                 * from pool.
                 */
		should_exit = 0;
		if (fiber_worker_should_handle_flag_wait(
			    pool, FIBER_ATOMIC_ACQUIRE)) {
			fiber_worker_handle_flag_wait(pool);
		}
		if (fiber_worker_should_handle_flag_kill(
			    pool, FIBER_ATOMIC_ACQUIRE)) {
			should_exit = should_exit ||
				      fiber_worker_handle_flag_kill(pool);
		}
		if (should_exit) {
			return;
		}
	}
}

static void fiber_worker_execute_job(struct fiber_pool *pool,
				     struct fiber_thread *thread,
				     struct fiber_job *job)
{
	(void)thread;
	do {
		tpsize to_kill;
		job->job_func(job->job_arg);

		/* Speed is important here. I am prioritizing speed over getting the
                 * most recent value 100% of the time by using RELAXED. This will be
                 * checked later (once there are no jobs on the queue) with a stronger
                 * memory ordering.
                 */
		to_kill = fiber_atomic_load(&pool->threads_kill_number,
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
	} while (pool->queue_ops.pop(pool->job_queue, job,
				     FIBER_QUEUE_NO_BLOCK) == 0);
}

static int
fiber_worker_should_handle_flag_kill(const struct fiber_pool *pool,
				     enum fiber_atomic_memorder load_memorder)
{
	tpsize to_kill;

	to_kill = fiber_atomic_load(&pool->threads_kill_number, load_memorder);
	return to_kill > 0;
}

static int
fiber_worker_should_handle_flag_wait(const struct fiber_pool *pool,
				     enum fiber_atomic_memorder load_memorder)
{
	/* TODO: Implement this */
	(void)pool;
	(void)load_memorder;
	return 0;
}

static int fiber_worker_handle_flag_kill(struct fiber_pool *pool)
{
	/* to_kill is the result after the subtraction */
	tpsize to_kill = fiber_atomic_dec_fetch(&pool->threads_kill_number,
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
		(void)fiber_atomic_inc_fetch(&pool->threads_kill_number,
					     FIBER_ATOMIC_ACQ_REL);
	}
	return 0;
}

static void fiber_worker_handle_flag_wait(struct fiber_pool *pool)
{
	/* TODO: Implement this */
	(void)pool;
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
	fiber_worker_should_handle_flag_kill,
	fiber_worker_should_handle_flag_wait,
	fiber_worker_handle_flag_kill,
	fiber_worker_handle_flag_wait,
	fiber_wake_runner,
};
#endif /* FIBER_BUILD_ENV_TEST */
