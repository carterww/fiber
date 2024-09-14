#ifndef FIBER_NO_DEFAULT_QUEUE
#ifndef _FIBER_FIFO_JOB_QUEUE_H
#define _FIBER_FIFO_JOB_QUEUE_H

#include <pthread.h>
#include <semaphore.h>

#include "job_queue.h"

struct fifo_jq {
	sem_t void_num;
	sem_t jobs_num;
	qsize head;
	qsize tail;
	struct fiber_job *jobs;
	qsize capacity;
	void (*free)(void *);
};

int fiber_queue_fifo_init(void **queue, qsize pages, void *(*malloc)(size_t),
			  void (*free)(void *));

int fiber_queue_fifo_push(void *queue, struct fiber_job *job, uint32_t flags);

int fiber_queue_fifo_pop(void *queue, struct fiber_job *buffer, uint32_t flags);

void fiber_queue_fifo_free(void *queue);

qsize fiber_queue_fifo_length(void *queue);

#endif // _FIBER_FIFO_JOB_QUEUE_H
#endif // FIBER_NO_DEFAULT_QUEUE
