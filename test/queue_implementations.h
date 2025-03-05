#ifndef _FIBER_TEST_QUEUE_IMPLEMENTATIONS_H
#define _FIBER_TEST_QUEUE_IMPLEMENTATIONS_H

#include "fiber.h"
#include "fiber_fifo.h"

#include "test/unity.h"

static struct fiber_queue_operations queue_operations_all[] = {
	FIBER_FIFO_QUEUE_OPERATIONS,
};

/* Simple way to run a test function for all queue implementations */
static void
test_queue_implementations_all(struct fiber_pool_init_options *base_opts,
			       void (*test_runner)(struct fiber_pool *pool))
{
	struct fiber_init_result res;
	unsigned long i;

	for (i = 0;
	     i < sizeof(queue_operations_all) / sizeof(*queue_operations_all);
	     ++i) {
		base_opts->queue_ops = &queue_operations_all[i];
		res = fiber_init(base_opts);
		TEST_ASSERT_EQUAL(0, res.error);
		TEST_ASSERT_NOT_NULL(res.pool);

		test_runner(res.pool);

		fiber_free(res.pool);
	}
}

#endif /* _FIBER_TEST_QUEUE_IMPLEMENTATIONS_H */
