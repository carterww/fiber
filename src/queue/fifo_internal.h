/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_FIFO_INTERNALS_H
#define _FIBER_FIFO_INTERNALS_H

#include "fiber/fiber.h"
#include "mutex.h"
#include "sem.h"

struct fiber_fifo_jq {
	fiber_semaphore void_num;
	fiber_semaphore jobs_num;
	fiber_mutex lock;
	struct fiber_job *jobs;
	qsize capacity;
	qsize head;
	qsize tail;
	free_function_t free;
};

#endif /* _FIBER_FIFO_INTERNALS_H */
