/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_FIFO_INTERNALS_H
#define _FIBER_FIFO_INTERNALS_H

#include "fiber.h"
#include "src/threading.h"

/* Definition of the opaque job queue pointer declared in fiber_fifo.h */
struct fiber_fifo_jq {
	fiber_semaphore void_num;
	fiber_semaphore jobs_num;
	struct fiber_job *jobs;
	qsize capacity;
	fiber_mutex head_lock;
	qsize head;
	fiber_mutex tail_lock;
	qsize tail;
	free_function_t free;
};

#endif /* _FIBER_FIFO_INTERNALS_H */
