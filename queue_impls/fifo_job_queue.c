/* See LICENSE file for copyright and license details. */

#ifndef FIBER_NO_DEFAULT_QUEUE
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

#include "fiber_utils.h"
#include "fifo_job_queue.h"
#include "../job_queue.h"

static const char *sem_post_err_msg =
	"sem_post returned error. Likely an overflow\n";

// From fiber.c
extern int __fiber_mutex_init_get_err(int error);
extern int __fiber_sem_init_get_err(int error);

int fiber_queue_fifo_init(void **queue, qsize capacity, void *(*malloc)(size_t),
			  void (*free)(void *))
{
	assert(queue != NULL, "fifo_init received a NULL queue");
	assert(capacity > 0, "fifo_init received a bad capacity");
	assert(malloc != NULL, "fifo_init received a NULL malloc func");
	assert(free != NULL, "fifo_init received a NULL malloc func");
	int error_code = 0;
	struct fifo_jq *fq = malloc(sizeof(*fq));
	if (fq == NULL) {
		error_code = ENOMEM;
		goto err;
	}
	struct fiber_job *jobs = malloc(capacity * sizeof(*jobs));
	if (jobs == NULL) {
		error_code = ENOMEM;
		goto err;
	}
	int sem_void_res = sem_init(&fq->void_num, 0, capacity);
	if (sem_void_res != 0) {
		error_code = __fiber_sem_init_get_err(errno);
		goto err;
	}
	int sem_jobs_res = sem_init(&fq->jobs_num, 0, 0);
	if (sem_jobs_res != 0) {
		error_code = __fiber_sem_init_get_err(errno);
		goto err;
	}

	fq->jobs = jobs;
	fq->head = 0;
	fq->tail = 0;
	fq->capacity = capacity;
	fq->free = free;
	*queue = fq;

	return 0;
err:
	if (jobs != NULL)
		free(jobs);
	if (sem_void_res == 0)
		sem_destroy(&fq->void_num);
	if (sem_jobs_res == 0)
		sem_destroy(&fq->jobs_num);
	if (fq != NULL)
		free(fq);
	return error_code;
}

int fiber_queue_fifo_push(void *queue, struct fiber_job *job, uint32_t flags)
{
	assert(queue != NULL, "fifo_push given NULL queue");
	assert(job != NULL, "fifo_push given NULL job");
	assert(job->job_func != NULL, "fifo_push given NULL job_func");
	struct fifo_jq *fq = (struct fifo_jq *)queue;
	// Decrement semaphore
	if (flags & FIBER_BLOCK) {
		while (sem_wait(&fq->void_num) == -1 && errno == EINTR)
			;
	} else {
		int try_res = sem_trywait(&fq->void_num);
		if (try_res == -1) {
			return -EAGAIN;
		}
	}

	fq->jobs[fq->tail] = *job;
	fq->tail = (fq->tail + 1) % fq->capacity;
	int lock_res = sem_post(&fq->jobs_num);
	assert(lock_res == 0, sem_post_err_msg);
	return 0;
}

int fiber_queue_fifo_pop(void *queue, struct fiber_job *buffer, uint32_t flags)
{
	assert(queue != NULL, "fifo_pop given NULL queue");
	assert(buffer != NULL, "fifo_pop given NULL job buffer");
	struct fifo_jq *fq = (struct fifo_jq *)queue;
	if (flags & FIBER_BLOCK) {
		while (sem_wait(&fq->jobs_num) == -1 && errno == EINTR)
			;
	} else {
		int try_res = sem_trywait(&fq->jobs_num);
		if (try_res == -1) {
			return EAGAIN;
		}
	}

	*buffer = fq->jobs[fq->head];
	fq->head = (fq->head + 1) % fq->capacity;
	int lock_res = sem_post(&fq->void_num);
	assert(lock_res == 0, sem_post_err_msg);
	return 0;
}

void fiber_queue_fifo_free(void *queue)
{
	assert(queue != NULL, "fifo_free given NULL queue");
	struct fifo_jq *fq = (struct fifo_jq *)queue;
	fq->free(fq->jobs);
	sem_destroy(&fq->jobs_num);
	sem_destroy(&fq->void_num);
	fq->free(fq);
}

qsize fiber_queue_fifo_length(void *queue)
{
	assert(queue != NULL, "fifo_length given NULL queue");
	struct fifo_jq *fq = (struct fifo_jq *)queue;
	int sem_val;
	int error_code = sem_getvalue(&fq->jobs_num, &sem_val);
	if (error_code != 0 || sem_val < 0) {
		return 0;
	}
	return sem_val;
}
#endif // FIBER_NO_DEFAULT_QUEUE
