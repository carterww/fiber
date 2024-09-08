#ifndef _FIBER_H
#define _FIBER_H

#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>

#include "job_queue.h"

typedef unsigned int tpsize; // Type to represent number of threads in pool
#define THREAD_POOL_SIZE_MAX UINT_MAX

/** Thread Management **/

struct fiber_thread {
	struct fiber_thread *next;
	pthread_t thread_id;
	jid job_id;
};

/** Pool **/

struct fiber_pool {
	pthread_mutex_t lock;
	jid job_id_prev;
	const struct fiber_queue_operations *queue_ops;
	void *job_queue;
	struct fiber_thread *thread_head;
	tpsize threads_number;
	tpsize threads_working;
	sem_t threads_sync;
	tpsize threads_kill_number;
	uint32_t pool_flags;
};

struct fiber_pool_init_options {
	struct fiber_queue_operations *queue_ops;
	tpsize threads_number;
	qsize queue_length;
};

int fiber_init(struct fiber_pool *pool, struct fiber_pool_init_options *opts);

jid fiber_job_push(struct fiber_pool *pool, struct fiber_job *job,
		   uint32_t queue_flags);

void fiber_free(struct fiber_pool *pool, uint32_t behavior_flags);

int fiber_threads_remove(struct fiber_pool *pool, tpsize threads_num,
			 uint32_t behavior_flags);

int fiber_threads_add(struct fiber_pool *pool, tpsize threads_num,
		      uint32_t behavior_flags);

tpsize fiber_threads_number(struct fiber_pool *pool);

tpsize fiber_threads_working(struct fiber_pool *pool);

void fiber_wait(struct fiber_pool *pool);

qsize fiber_jobs_pending(struct fiber_pool *pool);

#define FIBER_POOL_FLAG_WAIT (1 << 0)
#define FIBER_POOL_FLAG_KILL_N (1 << 1)

#endif // _FIBER_H
