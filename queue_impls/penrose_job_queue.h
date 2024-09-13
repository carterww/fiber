#ifndef _FIBER_PENROSE_JOB_QUEUE_H
#define _FIBER_PENROSE_JOB_QUEUE_H

#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>

#include "job_queue.h"

struct penrose_stair {
	struct penrose_stair *next;
	uint16_t start;
	uint16_t end;
	struct fiber_job jobs[];
};

struct penrose_jq {
	sem_t jobs_num_total;
	pthread_mutex_t stair_push_lock;
	struct penrose_stair *stair_push;
	pthread_mutex_t stair_pop_lock;
	struct penrose_stair *stair_pop;
	uint16_t stair_jobs_capacity;
	void *(*malloc)(size_t);
	void (*free)(void *);
};

#define PENROSE_STAIR_SIZE 2048
#define PENROSE_EXTRA_ALLOC (PENROSE_STAIR_SIZE - sizeof(struct penrose_stair))
#define penrose_page_job_capacity()                           \
	(PENROSE_STAIR_SIZE - sizeof(struct penrose_stair)) / \
		sizeof(struct fiber_job)

int fiber_queue_penrose_init(void **queue, qsize capacity,
			     void *(*malloc)(size_t), void (*free)(void *));

int fiber_queue_penrose_push(void *queue, struct fiber_job *job,
			     uint32_t flags);

int fiber_queue_penrose_pop(void *queue, struct fiber_job *buffer,
			    uint32_t flags);

void fiber_queue_penrose_free(void *queue);

qsize fiber_queue_penrose_length(void *queue);

#endif // _FIBER_PENROSE_JOB_QUEUE_H
