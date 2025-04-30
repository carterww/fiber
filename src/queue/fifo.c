/* See LICENSE file for copyright and license details. */

#include <limits.h>

#include "fiber/fiber.h"
#include "fiber/fiber_fifo.h"
#include "fiber_atomic/atomic.h"
#include "fiber_lock/semaphore.h"
#include "fifo_internal.h"
#include "src/debug.h"

struct fiber_queue_init_result fiber_queue_fifo_init(qsize capacity,
						     malloc_function_t _malloc,
						     free_function_t _free)
{
	struct fiber_fifo_jq *fq = NULL;
	struct fiber_job *jobs = NULL;
	int sem_void_res = 1;
	int sem_jobs_res = 1;
	struct fiber_queue_init_result res = { 0, NULL };

	fiber_assert(capacity > 0);
	fiber_assert((unsigned int)capacity <= UINT_MAX);
	fiber_assert(_malloc != NULL);
	fiber_assert(_free != NULL);

	fq = _malloc(sizeof(*fq));
	if (fq == NULL) {
		res.error = FBR_ENOMEM;
		goto err;
	}
	jobs = _malloc((unsigned long)capacity * sizeof(*jobs));
	if (jobs == NULL) {
		res.error = FBR_ENOMEM;
		goto err;
	}
	sem_void_res = fiber_sem_init(&fq->void_num, (unsigned int)capacity);
	if (sem_void_res != 0) {
		res.error = sem_void_res;
		goto err;
	}
	sem_jobs_res = fiber_sem_init(&fq->jobs_num, 0);
	if (sem_jobs_res != 0) {
		res.error = sem_jobs_res;
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

static qsize fiber_queue_fetch_increment(qsize *target, qsize cap)
{
	int lock_res;
	qsize target_old, target_new;

	target_old = fiber_atomic_load(target, FIBER_ATOMIC_ACQUIRE);
	do {
		target_new = (target_old + 1) % cap;
	} while (!fiber_atomic_cmp_xchng(target, &target_old, target_new, 1,
					 FIBER_ATOMIC_ACQ_REL,
					 FIBER_ATOMIC_ACQUIRE));
	return target_old;
}

int fiber_queue_fifo_push(void *queue, const struct fiber_job *job,
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

	/* Fetch old tail and increment its value */
	tail = fiber_queue_fetch_increment(&fq->tail, fq->capacity);

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

	/* Fetch old head and increment its value */
	head = fiber_queue_fetch_increment(&fq->head, fq->capacity);

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

	fq->free(fq);
}

#if defined(FIBER_BUILD_ENV_TEST)
#include "src/test_internal.h"
struct fiber_test_internal_queue_fifo fiber_test_internal_queue_fifo = {
	fiber_queue_sem_wait, fiber_queue_sem_trywait,
	fiber_queue_fetch_increment
};
#endif /* FIBER_BUILD_ENV_TEST */
