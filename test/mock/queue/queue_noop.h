/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_TEST_MOCK_QUEUE_NOOP_H
#define _FIBER_TEST_MOCK_QUEUE_NOOP_H

#include "fiber.h"

struct mock_queue_noop {
	int nothing;
};

static const struct mock_queue_noop mock_queue = { 0 };

static int mock_queue_noop_push(void *queue, struct fiber_job *job,
				unsigned long flags)
{
	(void)queue;
	(void)job;
	(void)flags;
	return 0;
}

static int mock_queue_noop_pop(void *queue, struct fiber_job *buffer,
			       unsigned long flags)
{
	(void)queue;
	(void)buffer;
	(void)flags;
	return 0;
}

static struct fiber_queue_init_result
mock_queue_noop_init(qsize capacity, malloc_function_t _malloc,
		     free_function_t _free)
{
	struct fiber_queue_init_result res;

	(void)capacity;
	(void)_malloc;
	(void)_free;

	res.error = 0;
	res.queue = (void *)&mock_queue;
	return res;
}

static void mock_queue_noop_free(void *queue)
{
	(void)queue;
	return;
}

static qsize mock_queue_noop_length(void *queue)
{
	(void)queue;
	return 0;
}

static const struct fiber_queue_operations mock_queue_noop_operations = {
	mock_queue_noop_push, mock_queue_noop_pop,    mock_queue_noop_init,
	mock_queue_noop_free, mock_queue_noop_length,
};

#endif /* _FIBER_TEST_MOCK_QUEUE_NOOP_H */
