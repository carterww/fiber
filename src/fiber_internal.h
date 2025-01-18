/* See LICENSE file for copyright and license details. */

#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>

#include "fiber.h"

#ifndef _FIBER_INTERNAL_H
#define _FIBER_INTERNAL_H

// Represents a single thread in a fiber_pool
struct fiber_thread {
	struct fiber_thread *next;
	pthread_t thread_id;
	jid job_id;
};

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
	void *(*malloc)(size_t size);
	void (*free)(void *ptr);
};

/** Flags **/

// Signal flags
#define FIBER_POOL_FLAG_WAIT (1 << 0)
#define FIBER_POOL_FLAG_KILL_N (1 << 1)

#endif // _FIBER_INTERNAL_H
