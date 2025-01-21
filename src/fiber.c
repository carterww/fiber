/* See LICENSE file for copyright and license details. */

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "atomic.h"
#include "fiber.h"
#include "fiber_internal.h"
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

static struct fiber_queue_operations *
fiber_init_queue_ops(struct fiber_queue_operations *ops,
		     void *(*malloc)(size_t));

static jid fiber_fetch_next_jid(jid *job_id_prev);

static int fiber_thread_pool_init(struct fiber_pool *pool,
				  tpsize threads_number);
static void fiber_thread_pool_free(struct fiber_pool *pool);

struct fiber_init_result fiber_init(struct fiber_pool_init_options *opts)
{
	int mutex_res = 1;
	int tp_init = 1;
	struct fiber_pool *pool = NULL;
	struct fiber_queue_init_result queue_res = {
		1,
		NULL,
	};

	struct fiber_init_result res = {
		0,
		NULL,
	};
	if (opts == NULL) {
		res.error = FBR_ENULL_ARGS;
		return res;
	}
	if (opts->threads_number < 1 || opts->queue_length < 1) {
		res.error = FBR_EINVLD_SIZE;
		return res;
	}
	if (opts->queue_ops == NULL) {
		res.error = FBR_EQUEOPS_NONE;
		return res;
	}
	if (opts->queue_ops->push == NULL || opts->queue_ops->pop == NULL ||
	    opts->queue_ops->init == NULL || opts->queue_ops->free == NULL) {
		res.error = FBR_EQUEOPS_NONE;
		return res;
	}

	{
		/* Reduce scope of these temp variables so we only use functions
		 * from the pool later on
                 */
		void *(*_malloc)(size_t) = opts->malloc == NULL ? malloc :
								  opts->malloc;
		void (*_free)(void *) = opts->free == NULL ? free : opts->free;
		pool = _malloc(sizeof(*pool));
		if (pool == NULL) {
			res.error = ENOMEM;
			return res;
		}
		pool->malloc = _malloc;
		pool->free = _free;
	}

	pool->queue_ops = fiber_init_queue_ops(opts->queue_ops, pool->malloc);
	if (pool->queue_ops == NULL) {
		res.error = FBR_EQUEOPS_NONE;
		return res;
	}
	mutex_res = fiber_mutex_init(&pool->lock);
	if (mutex_res != 0) {
		res.error = mutex_res;
		goto err;
	}

	pool->job_id_prev = -1;
	pool->pool_flags = 0;
	queue_res = pool->queue_ops->init(opts->queue_length, pool->malloc,
					  pool->free);
	if (queue_res.error != 0) {
		res.error = queue_res.error;
		goto err;
	}
	if (queue_res.queue == NULL) {
		res.error = FBR_EQUE_NULL;
		goto err;
	}
	pool->job_queue = queue_res.queue;
	tp_init = fiber_thread_pool_init(pool, opts->threads_number);
	if (tp_init != 0) {
		res.error = tp_init;
		goto err;
	}

	fiber_assert(res.error == 0);
	res.pool = pool;
	return res;
err:
	if (mutex_res == 0) {
		int des_res = fiber_mutex_destroy(&pool->lock);
		fiber_assert(des_res == 0);
	}
	if (queue_res.error == 0 && pool->job_queue != NULL) {
		pool->queue_ops->free(pool->job_queue);
	}
	if (pool->queue_ops != NULL) {
		pool->free((struct fiber_queue_operations *)pool->queue_ops);
	}
	return res;
}

jid fiber_job_push(struct fiber_pool *pool, struct fiber_job *job,
		   uint32_t queue_flags)
{
	if (unlikely(pool == NULL || job == NULL || job->job_func == NULL)) {
		return FBR_ENULL_ARGS;
	}
	job->job_id = fiber_fetch_next_jid(&pool->job_id_prev);
	fiber_assert(job->job_id > -1);
	return __fiber_job_push(pool, job, queue_flags);
}

void fiber_free(struct fiber_pool *pool)
{
	int des_res = 1;
	if (pool == NULL || pool->queue_ops == NULL ||
	    pool->job_queue == NULL || pool->queue_ops->free == NULL ||
	    pool->free == NULL) {
		return;
	}
	pool->queue_ops->free(pool->job_queue);
	fiber_thread_pool_free(pool);
	des_res = fiber_mutex_destroy(&pool->lock);
	fiber_assert(des_res == 0);
	pool->free((struct fiber_queue_operations *)pool->queue_ops);
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

/* THREAD CONTROL/INFO FUNCTIONS */

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
	int error_code;
	int start_res;
	int lock_res;
	if (pool == NULL) {
		return FBR_ENULL_ARGS;
	}
	if (threads_num < 1) {
		return FBR_EINVLD_SIZE;
	}
	fiber_assert(pool->threads_number + threads_num > 0);
	thread_list_result = fiber_thread_list_alloc(threads_num, pool->malloc);
	if (thread_list_result.error != 0) {
		return thread_list_result.error;
	}
	fiber_assert(thread_list_result.threads_head != NULL);
	start_res = fiber_workers_start(pool, thread_list_result.threads_head,
					threads_num);
	lock_res = fiber_mutex_lock(&pool->lock);
	fiber_assert(lock_res == 0);
	fiber_thread_list_add(&pool->thread_head,
			      thread_list_result.threads_head);
	fiber_mutex_unlock(&pool->lock);
	if (start_res != 0) {
		return start_res;
	}
	(void)atomic_add_fetch_tpsize(&pool->threads_number, threads_num,
				      FIBER_ATOMIC_SEQ_CST);
	return 0;
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

static struct fiber_queue_operations *
fiber_init_queue_ops(struct fiber_queue_operations *ops,
		     void *(*_malloc)(size_t))
{
	struct fiber_queue_operations *a_ops;
	a_ops = _malloc(sizeof(*ops));
	if (a_ops == NULL) {
		return NULL;
	}
	a_ops->push = ops->push;
	a_ops->pop = ops->pop;
	a_ops->init = ops->init;
	a_ops->free = ops->free;
	a_ops->length = ops->length;
	return a_ops;
}

static jid fiber_fetch_next_jid(jid *job_id_prev)
{
#if FIBER_CHECK_JID_OVERFLOW != 0
	jid next;
	jid prev = atomic_load_jid(job_id_prev, FIBER_ATOMIC_SEQ_CST);
	do {
		next = prev == FIBER_JID_MAX ? -1 : prev;
	} while (!atomic_compare_exchange_jid(job_id_prev, &prev, next, 1,
					      FIBER_ATOMIC_SEQ_CST,
					      FIBER_ATOMIC_SEQ_CST));
#endif
	return atomic_add_fetch_jid(job_id_prev, 1, FIBER_ATOMIC_SEQ_CST);
}

static int fiber_thread_pool_init(struct fiber_pool *pool,
				  tpsize threads_number)
{
	struct fiber_thread_list_init_result fiber_thread_list;
	int sem_res = 1;
	int error_code = 0;

	fiber_thread_list =
		fiber_thread_list_alloc(threads_number, pool->malloc);
	if (fiber_thread_list.error != 0) {
		goto err;
	}
	pool->thread_head = fiber_thread_list.threads_head;

	sem_res = fiber_sem_init(&pool->threads_sync, 0);
	if (sem_res != 0) {
		error_code = sem_res;
		goto err;
	}
	pool->threads_number = threads_number;
	pool->threads_working = 0;
	pool->threads_kill_number = 0;

	error_code =
		fiber_workers_start(pool, pool->thread_head, threads_number);
	if (error_code != 0) {
		goto err;
	}
	return 0;
err:
	if (pool->thread_head) {
		fiber_thread_list_free(pool->thread_head, threads_number,
				       pool->free);
	}
	if (sem_res == 0) {
		int des_res = fiber_sem_destroy(&pool->threads_sync);
		fiber_assert(des_res == 0);
	}
	return error_code;
}

static void fiber_thread_pool_free(struct fiber_pool *pool)
{
	int des_res;

	fiber_workers_cancel(pool->thread_head, FIBER_TPSIZE_MAX);
	fiber_thread_list_free(pool->thread_head, FIBER_TPSIZE_MAX, pool->free);

	des_res = fiber_sem_destroy(&pool->threads_sync);
	fiber_assert(des_res == 0);
}
