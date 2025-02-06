/* See LICENSE file for copyright and license details. */

#include "fiber.h"
#include "fiber_fifo.h"
#include "src/threading.h"
#include "src/utils.h"

/* Definition of the opaque job queue pointer declared in fiber_fifo.h */
struct fifo_jq {
	fiber_semaphore void_num;
	fiber_semaphore jobs_num;
	qsize head;
	qsize tail;
	struct fiber_job *jobs;
	qsize capacity;
	void (*free)(void *);
};

struct fiber_queue_init_result fiber_queue_fifo_init(qsize capacity,
						     void *(*_malloc)(size_t),
						     void (*_free)(void *))
{
	struct fifo_jq *fq = NULL;
	struct fiber_job *jobs = NULL;
	int sem_void_res = 1;
	int sem_jobs_res = 1;
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
	} while (wait_res == FBR_ETHREADING_EINTR);
	/* Make sure we didn't exit loop from error */
	fiber_assert(wait_res == 0);
}

static int fiber_queue_sem_trywait(fiber_semaphore *sem)
{
	int try_res;
	do {
		try_res = fiber_sem_trywait(sem);
	} while (try_res == FBR_ETHREADING_EINTR);
	if (try_res == FBR_ETHREADING_EAGAIN) {
		return 0;
	}
	fiber_assert(try_res == 0);
	return 1;
}

int fiber_queue_fifo_push(void *queue, struct fiber_job *job, uint32_t flags)
{
	struct fifo_jq *fq = (struct fifo_jq *)queue;
	int post_res = 1;

	fiber_assert(queue != NULL);
	fiber_assert(job != NULL);

	/* Decrement semaphore */
	if (flags & FIBER_QUEUE_BLOCK) {
		fiber_queue_sem_wait(&fq->void_num);
	} else {
		int sem_waited_success = fiber_queue_sem_trywait(&fq->void_num);
		if (!sem_waited_success) {
			return FBR_ETHREADING_EAGAIN;
		}
	}

	fq->jobs[fq->tail] = *job;
	fq->tail = (fq->tail + 1) % fq->capacity;
	post_res = fiber_sem_post(&fq->jobs_num);
	fiber_assert(post_res == 0);
	return 0;
}

int fiber_queue_fifo_pop(void *queue, struct fiber_job *buffer, uint32_t flags)
{
	struct fifo_jq *fq = (struct fifo_jq *)queue;
	int post_res = 1;

	fiber_assert(queue != NULL);
	fiber_assert(buffer != NULL);

	if (flags & FIBER_QUEUE_BLOCK) {
		fiber_queue_sem_wait(&fq->jobs_num);
	} else {
		int sem_waited_success = fiber_queue_sem_trywait(&fq->jobs_num);
		if (!sem_waited_success) {
			return FBR_ETHREADING_EAGAIN;
		}
	}

	*buffer = fq->jobs[fq->head];
	fq->head = (fq->head + 1) % fq->capacity;
	post_res = fiber_sem_post(&fq->void_num);
	fiber_assert(post_res == 0);
	return 0;
}

void fiber_queue_fifo_free(void *queue)
{
	int des_res;
	struct fifo_jq *fq = (struct fifo_jq *)queue;

	fiber_assert(queue != NULL);
	fq->free(fq->jobs);

	des_res = fiber_sem_destroy(&fq->jobs_num);
	fiber_assert(des_res == 0);

	des_res = fiber_sem_destroy(&fq->void_num);
	fiber_assert(des_res == 0);

	fq->free(fq);
}

qsize fiber_queue_fifo_length(void *queue)
{
	struct fifo_jq *fq = (struct fifo_jq *)queue;
	int sem_val;
	int error_code;

	fiber_assert(queue != NULL);

	error_code = fiber_sem_getvalue(&fq->jobs_num, &sem_val);

	if (error_code != 0 || sem_val <= 0) {
		return 0;
	}
	return sem_val;
}
