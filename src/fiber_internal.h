/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_INTERNAL_H
#define _FIBER_INTERNAL_H

#include "fiber.h"
#include "threading.h"

/* Artificial limits that can be set by the user */
#define FIBER_QUEUE_LENGTH_INIT_MIN (1)
#define FIBER_QUEUE_LENGTH_INIT_MAX (FIBER_QSIZE_MAX)

#define FIBER_THREADS_NUMBER_INIT_MIN (0)
#define FIBER_THREADS_NUMBER_INIT_MAX (FIBER_TPSIZE_MAX)

/* Represents a single thread in a fiber_pool */
struct fiber_thread {
	struct fiber_thread *next;
	tid thread_id;
	jid job_id; /* Only atomic LDR/STR (RELAXED used right now) */
};

/* A pool of threads and a job queue */
struct fiber_pool {
	fiber_mutex lock;
	jid job_id_prev; /* Only atomic LDR/STR */
	struct fiber_queue_operations queue_ops;
	void *job_queue;
	struct fiber_thread *thread_head; /* Guarded by pool's mutex */
	tpsize threads_number; /* Only atomic LDR/STR */
	tpsize threads_working; /* Only atomic LDR/STR */
	tpsize threads_kill_number; /* Only atomic LDR/STR */
	malloc_function_t malloc;
	free_function_t free;
};

#endif /* _FIBER_INTERNAL_H */
