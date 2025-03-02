/* See LICENSE file for copyright and license details. */

#include <stdlib.h>
#include <string.h>

#include "atomic.h"
#include "fiber.h"
#include "fiber_internal.h"
#include "thread_list.h"
#include "utils.h"
#include "worker.h"

/* Helper function associated with fiber_job_push. It only pushes the job to the
 * queue and returns the job id. Unlike fiber_job_push, it does not set the job
 * id.
 * @param pool -> Pool to push the job to.
 * @param job -> The job to push. It should already have the members set.
 * @param queue_flags -> Flags to pass to the job queue's push function.
 * @returns -> Either a valid job id (0 or more) or an error.
 * @error FBR_EPUSH_JOB -> A generic error returned by the queue push function.
 * @error FBR_EAGAIN -> The queue is full and FIBER_QUEUE_BLOCK was not specified
 * in queue_flags.
 * @note worker.c uses this to push jobs with preset job ids
 */
jid __fiber_job_push(const struct fiber_pool *pool, const struct fiber_job *job,
		     unsigned long queue_flags);

static int
fiber_validate_init_options(const struct fiber_pool_init_options *opts);

static int fiber_init_queue(struct fiber_pool *pool,
			    const struct fiber_pool_init_options *opts);
static void fiber_free_queue(struct fiber_pool *pool);

static jid fiber_fetch_next_jid(jid *job_id_prev);

static int fiber_thread_pool_start_threads(struct fiber_pool *pool,
					   tpsize threads_number);
/* When freeing the pool, call this first. */
static void fiber_thread_pool_end_threads(const struct fiber_pool *pool,
					  struct fiber_thread *thread_head);

struct fiber_init_result fiber_init(const struct fiber_pool_init_options *opts)
{
	int mutex_res = 1;
	int sem_res = 1;
	int tp_start = 1;
	int queue_init = 1;
	struct fiber_pool *pool = NULL;
	struct fiber_init_result res = { 0, NULL };

	int opts_valid = fiber_validate_init_options(opts);
	if (opts_valid != 0) {
		res.error = opts_valid;
		return res;
	}

	/* fiber_validate_init_options ensures opts->malloc is not NULL */
	pool = opts->malloc(sizeof(*pool));
	if (pool == NULL) {
		res.error = FBR_ENOMEM;
		return res;
	}

	/* Initialize primitive pool values */
	pool->job_id_prev = -1;
	pool->queue_ops.push = NULL;
	pool->queue_ops.pop = NULL;
	pool->queue_ops.init = NULL;
	pool->queue_ops.free = NULL;
	pool->queue_ops.length = NULL;
	pool->job_queue = NULL;
	pool->thread_head = NULL;
	pool->threads_number = 0;
	pool->threads_working = 0;
	pool->threads_kill_number = 0;
	pool->fiber_wait_callers = 0;
	pool->malloc = opts->malloc;
	pool->free = opts->free;

	/* Initialize the pool lock and syncing semaphore */
	mutex_res = fiber_mutex_init(&pool->lock);
	if (mutex_res != 0) {
		res.error = mutex_res;
		goto err;
	}
	sem_res = fiber_sem_init(&pool->threads_sync, 0);
	if (sem_res != 0) {
		res.error = sem_res;
		goto err;
	}

	/* Initialize the queue */
	queue_init = fiber_init_queue(pool, opts);
	if (queue_init != 0) {
		res.error = queue_init;
		goto err;
	}

	/* Start the threads. */
	tp_start = fiber_thread_pool_start_threads(pool, opts->threads_number);
	if (tp_start != 0) {
		res.error = tp_start;
		goto err;
	}

	fiber_assert(res.error == 0);
	res.pool = pool;
	return res;
err:
	/* Cleans up after fiber_thread_pool_start_threads */
	fiber_thread_pool_end_threads(pool, pool->thread_head);
	/* Cleans up after fiber_init_queue */
	fiber_free_queue(pool);

	/* Only call destroy on mutexes and sems if we know they were initialized */
	if (mutex_res == 0) {
		int des_res = fiber_mutex_destroy(&pool->lock);
		fiber_assert(des_res == 0);
	}
	if (sem_res == 0) {
		int des_res = fiber_sem_destroy(&pool->threads_sync);
		fiber_assert(des_res == 0);
	}
	return res;
}

jid fiber_job_push(struct fiber_pool *pool, struct fiber_job *job,
		   unsigned long queue_flags)
{
	if (pool == NULL || job == NULL || job->job_func == NULL) {
		return FBR_ENULL_ARGS;
	}
	job->job_id = fiber_fetch_next_jid(&pool->job_id_prev);
	return __fiber_job_push(pool, job, queue_flags);
}

void fiber_free(struct fiber_pool *pool)
{
	int des_res = 1;

	if (pool == NULL || pool->free == NULL) {
		return;
	}
	fiber_thread_pool_end_threads(pool, pool->thread_head);
	/* If queue_ops is not NULL, some of the queue was initialized. This function
         * can figure out what parts to free/cleanup.
         */
	fiber_free_queue(pool);

	des_res = fiber_mutex_destroy(&pool->lock);
	fiber_assert(des_res == 0);
	des_res = fiber_sem_destroy(&pool->threads_sync);
	fiber_assert(des_res == 0);

	pool->free(pool);
}

int fiber_wait(struct fiber_pool *pool)
{
	tpsize threads_working;
	qsize queue_length;
	tpsize threads_number;

	if (pool == NULL) {
		return FBR_ENULL_ARGS;
	}

	(void)atomic_add_fetch_tpsize(&pool->fiber_wait_callers, 1,
				      FIBER_ATOMIC_ACQ_REL);
	/* These loads must come after incrementing the value. These values
         * can change after we load them, but that is ok. All we are interested
         * in is grabbing a "snapshot" of these values AFTER we incremented
         * fiber_wait_callers.
         */
	threads_working = atomic_load_tpsize(&pool->threads_working,
					     FIBER_ATOMIC_ACQUIRE);
	queue_length = pool->queue_ops.length(pool->job_queue);
	threads_number = atomic_load_tpsize(&pool->threads_working,
					    FIBER_ATOMIC_ACQUIRE);

	/* There is a case where threads_number goes to zero after loading its
         * value and queue_length is > 0. This will result in a deadlock. In this
         * case, the last thread in the pool with post to the sem prior to cleaning
         * up.
         */
	if (threads_number > 0) {
		if (threads_working > 0 || queue_length > 0) {
			while (fiber_sem_wait(&pool->threads_sync) == FBR_EINTR)
				;
		}
	}
	return 0;
}

qsize fiber_jobs_pending(const struct fiber_pool *pool)
{
	if (pool == NULL || pool->job_queue == NULL) {
		return FBR_ENULL_ARGS;
	}
	if (pool->queue_ops.length == NULL) {
		return FBR_EQUEOPS_NONE;
	}
	return pool->queue_ops.length(pool->job_queue);
}

int fiber_threads_remove(struct fiber_pool *pool, tpsize threads_num)
{
	if (pool == NULL) {
		return FBR_ENULL_ARGS;
	}
	if (threads_num < 1) {
		return FBR_EINVLD_SIZE;
	}
	if (pool->queue_ops.push == NULL) {
		return FBR_EPOOL_UNINIT;
	}
	(void)atomic_add_fetch_tpsize(&pool->threads_kill_number, threads_num,
				      FIBER_ATOMIC_ACQ_REL);
	/* This wakes a sleeping worker thread (if one exists). Once the worker
         * wakes up, it will check the pool's flags and see it needs to terminate
         * itself.
         */
	fiber_worker_wake_other(pool);
	return 0;
}

int fiber_threads_add(struct fiber_pool *pool, tpsize threads_num)
{
	struct fiber_thread_list_init_result thread_list_result;
	struct fiber_thread *threads;
	int start_res;
	int lock_res;

	/* This value needs to be initialized in case of early workers_start_err goto */
	thread_list_result.threads_head = NULL;

	if (pool == NULL) {
		return FBR_ENULL_ARGS;
	}
	if (threads_num < 1) {
		return FBR_EINVLD_SIZE;
	}

	thread_list_result = fiber_thread_list_alloc(threads_num, pool->malloc);
	if (thread_list_result.error != 0) {
		return thread_list_result.error;
	}
	fiber_assert(thread_list_result.threads_head != NULL);
	start_res = fiber_workers_start(pool, thread_list_result.threads_head,
					threads_num);
	if (start_res != 0) {
		goto workers_start_err;
	}
	lock_res = fiber_mutex_lock(&pool->lock);
	fiber_assert(lock_res == 0);
	fiber_thread_list_add(&pool->thread_head,
			      thread_list_result.threads_head);
	lock_res = fiber_mutex_unlock(&pool->lock);
	fiber_assert(lock_res == 0);
	return 0;
workers_start_err:
	/* Failed to start workers. Need to cancel any that were started and free the
         * thread_list we just alloated. This function does both.
         */
	fiber_thread_pool_end_threads(pool, thread_list_result.threads_head);
	return start_res;
}

tpsize fiber_threads_number(const struct fiber_pool *pool)
{
	if (pool == NULL) {
		return FBR_ENULL_ARGS;
	}
	return atomic_load_tpsize(&pool->threads_number, FIBER_ATOMIC_ACQUIRE);
}

tpsize fiber_threads_working(const struct fiber_pool *pool)
{
	if (pool == NULL) {
		return FBR_ENULL_ARGS;
	}
	return atomic_load_tpsize(&pool->threads_working, FIBER_ATOMIC_ACQUIRE);
}

jid __fiber_job_push(const struct fiber_pool *pool, const struct fiber_job *job,
		     unsigned long queue_flags)
{
	int push_res;

	fiber_assert(pool->queue_ops.push != NULL);
	push_res = pool->queue_ops.push(pool->job_queue, job, queue_flags);
	switch (push_res) {
	case 0:
		break;
	case FBR_EPUSH_JOB:
	case FBR_EAGAIN:
		fiber_assert(push_res < 0);
		return push_res;
	default:
		panic(1);
	}
	return job->job_id;
}

static int
fiber_validate_init_options(const struct fiber_pool_init_options *opts)
{
	if (opts == NULL) {
		return FBR_ENULL_ARGS;
	}
	if (opts->threads_number < FIBER_THREADS_NUMBER_INIT_MIN ||
	    opts->threads_number > FIBER_THREADS_NUMBER_INIT_MAX ||
	    opts->queue_length < FIBER_QUEUE_LENGTH_INIT_MIN ||
	    opts->queue_length > FIBER_QUEUE_LENGTH_INIT_MAX) {
		return FBR_EINVLD_SIZE;
	}
	if (opts->queue_ops == NULL) {
		return FBR_EQUEOPS_NONE;
	}
	if (opts->queue_ops->push == NULL || opts->queue_ops->pop == NULL ||
	    opts->queue_ops->init == NULL || opts->queue_ops->free == NULL ||
	    opts->queue_ops->length == NULL) {
		return FBR_EQUEOPS_NONE;
	}
	if (opts->malloc == NULL || opts->free == NULL) {
		return FBR_ENO_ALLOC;
	}
	return 0;
}

static int fiber_init_queue(struct fiber_pool *pool,
			    const struct fiber_pool_init_options *opts)
{
	struct fiber_queue_init_result queue_init_res;
	const struct fiber_queue_operations *ops;

	fiber_assert(pool != NULL);
	fiber_assert(opts != NULL);
	fiber_assert(opts->malloc != NULL);
	fiber_assert(opts->free != NULL);
	fiber_assert(opts->queue_ops != NULL);

	ops = opts->queue_ops;

	pool->queue_ops.push = ops->push;
	pool->queue_ops.pop = ops->pop;
	pool->queue_ops.init = ops->init;
	pool->queue_ops.free = ops->free;
	pool->queue_ops.length = ops->length;

	queue_init_res =
		ops->init(opts->queue_length, opts->malloc, opts->free);
	if (queue_init_res.error != 0 || queue_init_res.queue == NULL) {
		return queue_init_res.error;
	}
	pool->job_queue = queue_init_res.queue;

	return 0;
}

static void fiber_free_queue(struct fiber_pool *pool)
{
	if (pool->job_queue == NULL) {
		return;
	}

	fiber_assert(pool->free != NULL);
	/* If job_queue is not NULL free should be set */
	fiber_assert(pool->queue_ops.free != NULL);
	pool->queue_ops.free(pool->job_queue);
}

static jid fiber_fetch_next_jid(jid *job_id_prev)
{
	/* The atomic operations in this function do not require total ordering
         * because we only need to ensure job_id_prev ops are ordered correctly.
         * Relaxed cannot be used because another thread may be reading and/or
         * modifying job_id_prev.
         */
	jid j;
#if FIBER_JID_MAX <= 2147483647 /* Max signed 32 bit value */
	jid next;
	jid prev = atomic_load_jid(job_id_prev, FIBER_ATOMIC_ACQUIRE);
	do {
		/* Failure of atomic_cmpxchg places job_id_prev's value into
                 * prev. Don't need to load on retries.
                 */
		next = prev == FIBER_JID_MAX ? -1 : prev;
	} while (!atomic_compare_exchange_jid(job_id_prev, &prev, next, 1,
					      FIBER_ATOMIC_ACQ_REL,
					      FIBER_ATOMIC_ACQUIRE));
#endif
	j = atomic_add_fetch_jid(job_id_prev, 1, FIBER_ATOMIC_ACQ_REL);
	fiber_assert(j >= 0);
	return j;
}

static int fiber_thread_pool_start_threads(struct fiber_pool *pool,
					   tpsize threads_number)
{
	int error_code = 0;
	struct fiber_thread_list_init_result fiber_thread_list;

	if (threads_number <= 0) {
		return 0;
	}

	fiber_thread_list =
		fiber_thread_list_alloc(threads_number, pool->malloc);
	if (fiber_thread_list.error != 0) {
		return fiber_thread_list.error;
	}
	pool->thread_head = fiber_thread_list.threads_head;

	error_code =
		fiber_workers_start(pool, pool->thread_head, threads_number);
	if (error_code != 0) {
		return error_code;
	}
	return 0;
}

static void fiber_thread_pool_end_threads(const struct fiber_pool *pool,
					  struct fiber_thread *thread_head)
{
	if (thread_head != NULL) {
		fiber_assert(pool->free != NULL);
		fiber_workers_cancel(thread_head, FIBER_TPSIZE_MAX);
	}
}

#if defined(FIBER_BUILD_ENV_TEST)
#include "test_internal.h"
struct fiber_test_internal_fiber fiber_test_internal_fiber = {
	__fiber_job_push,
	fiber_validate_init_options,
	fiber_init_queue,
	fiber_free_queue,
	fiber_fetch_next_jid,
	fiber_thread_pool_start_threads,
	fiber_thread_pool_end_threads
};
#endif /* FIBER_BUILD_ENV_TEST */
