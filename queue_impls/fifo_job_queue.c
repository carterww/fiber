#ifndef FIBER_NO_DEFAULT_QUEUE
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

#include "fifo_job_queue.h"
#include "../job_queue.h"
#include <unistd.h>

static const char *sem_post_err_msg =
	"sem_post returned error. Likely an overflow\n";

// From fiber.c
extern void __fiber_die(const char *msg, int fd, int exit_code);
extern int __fiber_mutex_init_get_err(int error);
extern int __fiber_sem_init_get_err(int error);

int fiber_queue_fifo_init(void **queue, qsize capacity, void *(*malloc)(size_t),
			  void (*free)(void *))
{
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
	int mutex_res = pthread_mutex_init(&fq->lock, NULL);
	if (mutex_res != 0) {
		error_code = __fiber_mutex_init_get_err(mutex_res);
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
	if (fq != NULL)
		free(fq);
	if (jobs != NULL)
		free(jobs);
	if (mutex_res == 0)
		pthread_mutex_destroy(&fq->lock);
	if (sem_void_res == 0)
		sem_destroy(&fq->void_num);
	if (sem_jobs_res == 0)
		sem_destroy(&fq->jobs_num);
	return error_code;
}

int fiber_queue_fifo_push(void *queue, struct fiber_job *job, uint32_t flags)
{
	struct fifo_jq *fq = (struct fifo_jq *)queue;
	// Decrement semaphore
	if (flags & FIBER_BLOCK) {
		while (sem_wait(&fq->void_num) == -1 && errno == EINTR)
			;
	} else {
		int try_res = sem_trywait(&fq->void_num);
		if (try_res == -1) {
			return EAGAIN;
		}
	}

	int lock_res = pthread_mutex_lock(&fq->lock);
	if (lock_res != 0 && lock_res != EDEADLK) {
		return lock_res;
	}
	// Critical section
	fq->jobs[fq->tail] = *job;
	fq->tail = (fq->tail + 1) % fq->capacity;
	pthread_mutex_unlock(&fq->lock);
	lock_res = sem_post(&fq->jobs_num);
	if (lock_res != 0) {
		__fiber_die(sem_post_err_msg, STDERR_FILENO, errno);
		return lock_res;
	}
	return 0;
}

int fiber_queue_fifo_pop(void *queue, struct fiber_job *buffer, uint32_t flags)
{
	struct fifo_jq *fq = (struct fifo_jq *)queue;
	if (flags & FIBER_BLOCK) {
		while (sem_wait(&fq->jobs_num) == -1) {
			if (errno == EINTR) {
				return EINTR;
			}
		}
	} else {
		int try_res = sem_trywait(&fq->jobs_num);
		if (try_res == -1) {
			return EAGAIN;
		}
	}

	int lock_res = pthread_mutex_lock(&fq->lock);
	if (lock_res != 0 && lock_res != EDEADLK) {
		return lock_res;
	}
	// Critical section
	*buffer = fq->jobs[fq->head];
	fq->head = (fq->head + 1) % fq->capacity;
	pthread_mutex_unlock(&fq->lock);
	lock_res = sem_post(&fq->void_num);
	if (lock_res != 0) {
		__fiber_die(sem_post_err_msg, STDERR_FILENO, errno);
		return lock_res;
	}
	return 0;
}

void fiber_queue_fifo_free(void *queue)
{
	struct fifo_jq *fq = (struct fifo_jq *)queue;
	fq->capacity = 0;
	fq->free(fq->jobs);
	pthread_mutex_destroy(&fq->lock);
	sem_destroy(&fq->jobs_num);
	sem_destroy(&fq->void_num);
	fq->free(fq);
}

qsize fiber_queue_fifo_capacity(void *queue)
{
	struct fifo_jq *fq = (struct fifo_jq *)queue;
	return fq->capacity;
}

qsize fiber_queue_fifo_length(void *queue)
{
	struct fifo_jq *fq = (struct fifo_jq *)queue;
	int sem_val;
	int error_code = sem_getvalue(&fq->jobs_num, &sem_val);
	if (error_code != 0 || sem_val < 0) {
		return 0;
	}
	return sem_val;
}
#endif // FIBER_NO_DEFAULT_QUEUE
