/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_INTERNAL_H
#define _FIBER_INTERNAL_H

#include <stdint.h>

#include "fiber.h"
#include "threading.h"

/* Represents a single thread in a fiber_pool */
struct fiber_thread {
	struct fiber_thread *next;
	tid thread_id;
	jid job_id; /* Only LDR/STR this through atomic (RELAXED used right now) */
};

/* A pool of threads and a job queue */
struct fiber_pool {
	fiber_mutex lock;
	jid job_id_prev; /* Only LDR/STR this through atomic */
	struct fiber_queue_operations *queue_ops;
	void *job_queue;
	struct fiber_thread *thread_head;
	tpsize threads_number; /* Only LDR/STR this through atomic */
	tpsize threads_working; /* Only LDR/STR this through atomic */
	fiber_semaphore threads_sync;
	tpsize threads_kill_number; /* Only LDR/STR this through atomic */
	uint32_t pool_flags; /* Only LDR/STR this through atomic */
	void *(*malloc)(size_t size);
	void (*free)(void *ptr);
};

/** Flags **/

/* Signal flags */
#define FIBER_POOL_FLAG_WAIT (1 << 0)
#define FIBER_POOL_FLAG_KILL_N (1 << 1)

#endif /* _FIBER_INTERNAL_H */
