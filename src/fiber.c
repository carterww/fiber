/* See LICENSE file for copyright and license details. */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "atomic.h"
#include "fiber.h"
#include "fiber_internal.h"
#include "threading.h"
#include "thread_list.h"
#include "utils.h"
#include "worker.h"

/* I included these directly for a couple reasons:
 * 1. Putting everything in one translation unit allows the compiler to optimize
 *    more effectively. I haven't measured this so I'm only speaking in generalities.
 *    It may be the case that it makes Fiber slower!
 * 2. Usually this makes compilation slower because we cannot use an unmodified C file's
 *    previous object file. In this case, these C files are very small so the overhead is
 *    minor.
 */
#include "atomic_gcc_clang.c"
#include "thread_list.c"
#include "version.c"
#include "worker.c"

#if FIBER_USE_PTHREADS != 0
#include "threading_pthread.c"
#else
#error "FIBER_USE_PTHREADS was disabled in fiber.h but there is no alternative threading implementation included."
#endif /* FIBER_USE_PTHREADS */

/* Helper function associated with fiber_job_push. It only pushes the job to the
 * queue and returns the job id. Unlike fiber_job_push, it does not set the job
 * id.
 * @param pool -> Pool to push the job to.
 * @param job -> The job to push. It should already have the members set.
 * @param queue_flags -> Flags to pass to the job queue's push function.
 * @returns -> Either a valid job id (0 or more) or an error.
 * @error FBR_EPUSH_JOB -> The job queue's push function returned a non zero value.
 * @note worker.c uses this to push jobs with preset job ids
 */
jid __fiber_job_push(struct fiber_pool *pool, struct fiber_job *job,
		     uint32_t queue_flags);

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

struct fiber_init_result fiber_init(struct fiber_pool_init_options *opts)
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
	pool->queue_ops = NULL;
	pool->job_queue = NULL;
	pool->thread_head = NULL;
	pool->threads_number = opts->threads_number;
	pool->threads_working = 0;
	pool->threads_kill_number = 0;
	pool->pool_flags = 0;
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

	/* Starts the threads. Cleans up after itself on error. */
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
		   uint32_t queue_flags)
{
	if (pool == NULL || job == NULL || job->job_func == NULL) {
		return FBR_ENULL_ARGS;
	}
	job->job_id = fiber_fetch_next_jid(&pool->job_id_prev);
	fiber_assert(job->job_id > -1);
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

void fiber_wait(struct fiber_pool *pool)
{
	tpsize working;
	tpsize length;
	uint32_t off;

	if (pool == NULL) {
		return;
	}
	(void)atomic_or_fetch_uint32(&pool->pool_flags, FIBER_POOL_FLAG_WAIT,
				     FIBER_ATOMIC_SEQ_CST);
	/* This sequence does not cause a race condition. If the number of
	 * working threads is non zero AFTER we set the pool flags, we
	 * know some thread will eventaully handle it. In the case where
	 * working is 0. The queue was either just empty or is empty.
         */
	working = atomic_load_tpsize(&pool->threads_working,
				     FIBER_ATOMIC_SEQ_CST);
	length = fiber_jobs_pending(pool);
	if (working > 0 || length > 0) {
		while (fiber_sem_wait(&pool->threads_sync) != 0 &&
		       errno == EINTR)
			;
	}
	off = ~FIBER_POOL_FLAG_WAIT;
	(void)atomic_and_fetch_uint32(&pool->pool_flags, off,
				      FIBER_ATOMIC_SEQ_CST);
}

qsize fiber_jobs_pending(struct fiber_pool *pool)
{
	if (pool == NULL || pool->job_queue == NULL ||
	    pool->queue_ops == NULL) {
		return FBR_ENULL_ARGS;
	}
	if (pool->queue_ops->length == NULL) {
		return FBR_EQUEOPS_NONE;
	}
	return pool->queue_ops->length(pool->job_queue);
}

int fiber_threads_remove(struct fiber_pool *pool, tpsize threads_num)
{
	if (pool == NULL) {
		return FBR_ENULL_ARGS;
	}
	if (threads_num < 1) {
		return FBR_EINVLD_SIZE;
	}
	if (pool->queue_ops == NULL || pool->queue_ops->push == NULL) {
		return FBR_EPOOL_UNINIT;
	}
	/* Set flag & val to notify thread it should commit seppuku */
	(void)atomic_add_fetch_tpsize(&pool->threads_kill_number, threads_num,
				      FIBER_ATOMIC_SEQ_CST);
	(void)atomic_or_fetch_uint32(&pool->pool_flags, FIBER_POOL_FLAG_KILL_N,
				     FIBER_ATOMIC_SEQ_CST);
	fiber_worker_wake_other(pool);
	return 0;
}

int fiber_threads_add(struct fiber_pool *pool, tpsize threads_num)
{
	struct fiber_thread_list_init_result thread_list_result;
	struct fiber_thread *threads;
	int start_res;
	int lock_res;
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
	(void)atomic_add_fetch_tpsize(&pool->threads_number, threads_num,
				      FIBER_ATOMIC_SEQ_CST);
	return 0;
workers_start_err:
	/* Failed to start workers. Need to cancel any that were started and free the
         * thread_list we just alloated. This function does both.
         */
	fiber_thread_pool_end_threads(pool, thread_list_result.threads_head);
	return start_res;
}

tpsize fiber_threads_number(struct fiber_pool *pool)
{
	if (pool == NULL) {
		return FBR_ENULL_ARGS;
	}
	return atomic_load_tpsize(&pool->threads_number, FIBER_ATOMIC_SEQ_CST);
}

tpsize fiber_threads_working(struct fiber_pool *pool)
{
	if (pool == NULL) {
		return FBR_ENULL_ARGS;
	}
	return atomic_load_tpsize(&pool->threads_working, FIBER_ATOMIC_SEQ_CST);
}

jid __fiber_job_push(struct fiber_pool *pool, struct fiber_job *job,
		     uint32_t queue_flags)
{
	int push_res;

	fiber_assert(pool->queue_ops != NULL && pool->queue_ops->push != NULL);
	push_res = pool->queue_ops->push(pool->job_queue, job, queue_flags);
	/* Don't allow positive error codes to return */
	if (push_res != 0) {
		return FBR_EPUSH_JOB;
	}
	return job->job_id;
}

static int
fiber_validate_init_options(const struct fiber_pool_init_options *opts)
{
	if (opts == NULL) {
		return FBR_ENULL_ARGS;
	}
	if (opts->threads_number < 1 || opts->queue_length < 1) {
		return FBR_EINVLD_SIZE;
	}
	if (opts->queue_ops == NULL) {
		return FBR_EQUEOPS_NONE;
	}
	if (opts->queue_ops->push == NULL || opts->queue_ops->pop == NULL ||
	    opts->queue_ops->init == NULL || opts->queue_ops->free == NULL) {
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

	pool->queue_ops = opts->malloc(sizeof(*pool->queue_ops));
	if (pool->queue_ops == NULL) {
		return FBR_ENOMEM;
	}
	pool->queue_ops->push = ops->push;
	pool->queue_ops->pop = ops->pop;
	pool->queue_ops->init = ops->init;
	pool->queue_ops->free = ops->free;
	pool->queue_ops->length = ops->length;

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
	if (pool->job_queue != NULL) {
		fiber_assert(pool->queue_ops->free != NULL);
		pool->queue_ops->free(pool->job_queue);
	}
	if (pool->queue_ops != NULL) {
		pool->free(pool->queue_ops);
	}
}

static jid fiber_fetch_next_jid(jid *job_id_prev)
{
	jid j;
#if FIBER_CHECK_JID_OVERFLOW != 0
	jid next;
	jid prev = atomic_load_jid(job_id_prev, FIBER_ATOMIC_SEQ_CST);
	do {
		next = prev == FIBER_JID_MAX ? -1 : prev;
	} while (!atomic_compare_exchange_jid(job_id_prev, &prev, next, 1,
					      FIBER_ATOMIC_SEQ_CST,
					      FIBER_ATOMIC_SEQ_CST));
#endif
	j = atomic_load_jid(job_id_prev, FIBER_ATOMIC_SEQ_CST);
	fiber_assert(j >= -1);
	return atomic_add_fetch_jid(job_id_prev, 1, FIBER_ATOMIC_SEQ_CST);
}

static int fiber_thread_pool_start_threads(struct fiber_pool *pool,
					   tpsize threads_number)
{
	int error_code = 0;
	struct fiber_thread_list_init_result fiber_thread_list;

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
	if (thread_head) {
		fiber_assert(pool->free != NULL);
		fiber_workers_cancel(thread_head, FIBER_TPSIZE_MAX);
	}
}
