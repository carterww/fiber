/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_QUEUE_FIFO_H
#define _FIBER_QUEUE_FIFO_H

#include <stdint.h>

#include "fiber.h"

struct fifo_jq;

struct fiber_queue_init_result fiber_queue_fifo_init(qsize pages,
						     void *(*malloc)(size_t),
						     void (*free)(void *));

int fiber_queue_fifo_push(void *queue, struct fiber_job *job, uint32_t flags);

int fiber_queue_fifo_pop(void *queue, struct fiber_job *buffer, uint32_t flags);

void fiber_queue_fifo_free(void *queue);

qsize fiber_queue_fifo_length(void *queue);

#define FIBER_FIFO_QUEUE_OPERATIONS                                   \
	{                                                             \
		fiber_queue_fifo_push, fiber_queue_fifo_pop,          \
			fiber_queue_fifo_init, fiber_queue_fifo_free, \
			fiber_queue_fifo_length,                      \
	}

#endif /* _FIBER_QUEUE_FIFO_H */
