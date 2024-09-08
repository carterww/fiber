#ifdef FIBER_COMPILE_FIFO
#ifndef _FIBER_FIFO_JOB_QUEUE_H
#define _FIBER_FIFO_JOB_QUEUE_H

#include <pthread.h>
#include <semaphore.h>

#include "../job_queue.h"

struct fifo_jq {
	pthread_mutex_t lock;
	struct fiber_job *jobs;
	qsize head;
	qsize tail;
	qsize capacity;
	sem_t void_num;
	sem_t jobs_num;
};

int fiber_queue_fifo_init(void **queue, qsize capacity);

int fiber_queue_fifo_push(void *queue, struct fiber_job *job, uint32_t flags);

int fiber_queue_fifo_pop(void *queue, struct fiber_job *buffer, uint32_t flags);

void fiber_queue_fifo_free(void *queue);

qsize fiber_queue_fifo_capacity(void *queue);

qsize fiber_queue_fifo_length(void *queue);

#endif // _FIBER_FIFO_JOB_QUEUE_H
#endif // FIBER_COMPILE_FIFO
