/* See LICENSE file for copyright and license details. */

#include "fiber.h"
#include "fiber_fifo.h"
#include "fifo_internal.h"
#include "src/threading.h"
#include "src/utils.h"

struct fiber_queue_init_result fiber_queue_fifo_init(qsize capacity,
						     malloc_function_t _malloc,
						     free_function_t _free)
{
	struct fiber_fifo_jq *fq = NULL;
	struct fiber_job *jobs = NULL;
	int sem_void_res = 1;
	int sem_jobs_res = 1;
	int head_lock_res = 1;
	int tail_lock_res = 1;
	struct fiber_queue_init_result res = { 0, NULL };

	fiber_assert(capacity > 0);
	fiber_assert(_malloc != NULL);
	fiber_assert(_free != NULL);

	fq = _malloc(sizeof(*fq));
	if (fq == NULL) {
		res.error = FBR_ENOMEM;
		goto err;
	}
	jobs = _malloc(capacity * sizeof(*jobs));
	if (jobs == NULL) {
		res.error = FBR_ENOMEM;
		goto err;
	}
	sem_void_res = fiber_sem_init(&fq->void_num, capacity);
	if (sem_void_res != 0) {
		res.error = sem_void_res;
		goto err;
	}
	sem_jobs_res = fiber_sem_init(&fq->jobs_num, 0);
	if (sem_jobs_res != 0) {
		res.error = sem_jobs_res;
		goto err;
	}
	head_lock_res = fiber_mutex_init(&fq->head_lock);
	if (head_lock_res != 0) {
		res.error = head_lock_res;
		goto err;
	}
	tail_lock_res = fiber_mutex_init(&fq->tail_lock);
	if (tail_lock_res != 0) {
		res.error = tail_lock_res;
		goto err;
	}

	fq->jobs = jobs;
	fq->head = 0;
	fq->tail = 0;
	fq->capacity = capacity;
	fq->free = _free;

	res.error = 0;
	res.queue = fq;
	return res;
err:
	if (fq != NULL) {
		_free(fq);
	}
	if (jobs != NULL) {
		_free(jobs);
	}
	if (sem_void_res == 0) {
		int des_res = fiber_sem_destroy(&fq->void_num);
		fiber_assert(des_res == 0);
	}
	if (sem_jobs_res == 0) {
		int des_res = fiber_sem_destroy(&fq->jobs_num);
		fiber_assert(des_res == 0);
	}
	if (head_lock_res == 0) {
		int des_res = fiber_mutex_destroy(&fq->head_lock);
		fiber_assert(des_res == 0);
	}
	if (tail_lock_res == 0) {
		int des_res = fiber_mutex_destroy(&fq->tail_lock);
		fiber_assert(des_res == 0);
	}
	return res;
}

static void fiber_queue_sem_wait(fiber_semaphore *sem)
{
	int wait_res;
	do {
		wait_res = fiber_sem_wait(sem);
	} while (wait_res == FBR_EINTR);
	/* Make sure we didn't exit loop from error */
	fiber_assert(wait_res == 0);
}

static int fiber_queue_sem_trywait(fiber_semaphore *sem)
{
	int try_res;
	do {
		try_res = fiber_sem_trywait(sem);
	} while (try_res == FBR_EINTR);
	if (try_res == FBR_EAGAIN) {
		return 0;
	}
	fiber_assert(try_res == 0);
	return 1;
}

static qsize fiber_queue_fetch_increment(fiber_mutex *mtx, qsize *target,
					 qsize cap)
{
	int lock_res;
	qsize target_current;

	lock_res = fiber_mutex_lock(mtx);
	fiber_assert(lock_res == 0);
	target_current = *target;
	*target = (target_current + 1) % cap;
	lock_res = fiber_mutex_unlock(mtx);
	fiber_assert(lock_res == 0);

	return target_current;
}

int fiber_queue_fifo_push(void *queue, struct fiber_job *job,
			  unsigned long flags)
{
	struct fiber_fifo_jq *fq = (struct fiber_fifo_jq *)queue;
	int post_res;
	int lock_res;
	qsize tail;

	fiber_assert(queue != NULL);
	fiber_assert(job != NULL);

	/* Decrement semaphore */
	if (flags & FIBER_QUEUE_BLOCK) {
		fiber_queue_sem_wait(&fq->void_num);
	} else {
		int sem_waited_success = fiber_queue_sem_trywait(&fq->void_num);
		if (!sem_waited_success) {
			return FBR_EAGAIN;
		}
	}

	/* Fetch old tail and increment it's value */
	tail = fiber_queue_fetch_increment(&fq->tail_lock, &fq->tail,
					   fq->capacity);

	fq->jobs[tail] = *job;
	post_res = fiber_sem_post(&fq->jobs_num);
	fiber_assert(post_res == 0);
	return 0;
}

int fiber_queue_fifo_pop(void *queue, struct fiber_job *buffer,
			 unsigned long flags)
{
	struct fiber_fifo_jq *fq = (struct fiber_fifo_jq *)queue;
	int post_res;
	int lock_res;
	int head;

	fiber_assert(queue != NULL);
	fiber_assert(buffer != NULL);

	if (flags & FIBER_QUEUE_BLOCK) {
		fiber_queue_sem_wait(&fq->jobs_num);
	} else {
		int sem_waited_success = fiber_queue_sem_trywait(&fq->jobs_num);
		if (!sem_waited_success) {
			return FBR_EAGAIN;
		}
	}

	/* Fetch old head and increment it's value */
	head = fiber_queue_fetch_increment(&fq->head_lock, &fq->head,
					   fq->capacity);

	*buffer = fq->jobs[head];
	post_res = fiber_sem_post(&fq->void_num);
	fiber_assert(post_res == 0);
	return 0;
}

void fiber_queue_fifo_free(void *queue)
{
	int des_res;
	struct fiber_fifo_jq *fq = (struct fiber_fifo_jq *)queue;

	fiber_assert(queue != NULL);
	fq->free(fq->jobs);

	des_res = fiber_sem_destroy(&fq->jobs_num);
	fiber_assert(des_res == 0);

	des_res = fiber_sem_destroy(&fq->void_num);
	fiber_assert(des_res == 0);

	des_res = fiber_mutex_destroy(&fq->head_lock);
	fiber_assert(des_res == 0);

	des_res = fiber_mutex_destroy(&fq->tail_lock);
	fiber_assert(des_res == 0);

	fq->free(fq);
}

qsize fiber_queue_fifo_length(void *queue)
{
	struct fiber_fifo_jq *fq = (struct fiber_fifo_jq *)queue;
	int sem_val;
	int error_code;

	fiber_assert(queue != NULL);

	error_code = fiber_sem_getvalue(&fq->jobs_num, &sem_val);

	if (error_code != 0 || sem_val <= 0) {
		return 0;
	}
	return sem_val;
}

#if defined(FIBER_BUILD_ENV_TEST)
#include "src/test_internal.h"
struct fiber_test_internal_queue_fifo fiber_test_internal_queue_fifo = {
	fiber_queue_sem_wait, fiber_queue_sem_trywait,
	fiber_queue_fetch_increment
};
#endif /* FIBER_BUILD_ENV_TEST */
