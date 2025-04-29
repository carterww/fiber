/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_INTERNAL_H
#define _FIBER_INTERNAL_H

#include "fiber/fiber.h"
#include "fiber_lock/mutex.h"
#include "threading.h"
#include "twql_packed.h"

/* Artificial limits that can be set by the user */
#define FIBER_QUEUE_LENGTH_INIT_MIN (1)
#define FIBER_QUEUE_LENGTH_INIT_MAX (FIBER_QSIZE_MAX)

#define FIBER_THREADS_NUMBER_INIT_MIN (0)
#define FIBER_THREADS_NUMBER_INIT_MAX (FIBER_TPSIZE_MAX)

/* Represents a single thread in a fiber_pool */
struct fiber_thread {
	struct fiber_thread *next;
	tid thread_id;
};

/* A pool of threads and a job queue */
struct fiber_pool {
	fiber_mutex lock;
	jid job_id_prev; /* Only atomic LDR/STR */
	struct fiber_queue_operations queue_ops;
	void *job_queue;
	struct fiber_thread *thread_head; /* Guarded by pool's mutex */
	tpsize threads_number; /* Only atomic LDR/STR */
	union fiber_twql_packed twql; /* Only use this through twql_packed.h */
	tpsize threads_kill_number; /* Only atomic LDR/STR */
	unsigned long fiber_wait_epoch; /* Only use this through epoch.h */
	malloc_function_t malloc;
	free_function_t free;
};

#endif /* _FIBER_INTERNAL_H */
