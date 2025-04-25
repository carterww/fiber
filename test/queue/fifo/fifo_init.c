#include <stdlib.h>

#include "fiber/fiber.h"
#include "fiber/fiber_fifo.h"
#include "fifo_validate.h"

#include "test/unity.h"
#include "test/mock/alloc/alloc_trace.h"
#include "test/mock/threading/threading_trace_fault.h"

static const qsize queue_length = 10;

void setUp(void)
{
	alloc_trace_reset(malloc, free);
	threading_trace_fault_reset();
}

void tearDown(void)
{
	alloc_trace_verify();
	threading_trace_fault_verify();
}

void test_fifo_init_valid(void)
{
	struct fiber_queue_init_result res;
	struct fiber_fifo_jq *jq;
	fiber_semaphore sem;

	res = fiber_queue_fifo_init(queue_length, alloc_trace_malloc,
				    alloc_trace_free);

	TEST_ASSERT_EQUAL(0, res.error);
	TEST_ASSERT_NOT_NULL(res.queue);
	jq = (struct fiber_fifo_jq *)res.queue;
	validate_queue(jq, queue_length, alloc_trace_free);
	fiber_queue_fifo_free(res.queue);
}

int main(void)
{
	UNITY_BEGIN();
	threading_trace_fault_init();
	alloc_trace_init();

	RUN_TEST(test_fifo_init_valid);

	alloc_trace_destroy();
	threading_trace_fault_destroy();
	return UNITY_END();
}
