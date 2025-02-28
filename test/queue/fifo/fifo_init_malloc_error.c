#include <stdlib.h>

#include "fiber.h"
#include "fiber_fifo.h"
#include "fifo_validate.h"
#include "src/queue/fifo_internal.h"

#include "test/mock/alloc/alloc_fault.h"
#include "test/mock/threading/threading_trace_fault.h"
#include "test/unity.h"

#define QUEUE_FIFO_MALLOC_COUNT (2)

#define test_fifo_init_malloc_fault_body(fail_after)                          \
	do {                                                                  \
		struct fiber_queue_init_result res;                           \
		struct fiber_fifo_jq *jq;                                     \
                                                                              \
		alloc_fault_reset(malloc, free, fail_after);                  \
                                                                              \
		res = fiber_queue_fifo_init(queue_length, alloc_fault_malloc, \
					    alloc_fault_free);                \
                                                                              \
		TEST_ASSERT_EQUAL(FBR_ENOMEM, res.error);                     \
		TEST_ASSERT_NULL(res.queue);                                  \
	} while (0)

static const qsize queue_length = 10;

void setUp(void)
{
	/* alloc_fault_reset called by each case */
	threading_trace_fault_reset();
}

void tearDown(void)
{
	alloc_fault_verify();
	threading_trace_fault_verify();
}

void test_fifo_init_malloc_fault0(void)
{
	test_fifo_init_malloc_fault_body(0);
}

void test_fifo_init_malloc_fault1(void)
{
	test_fifo_init_malloc_fault_body(1);
}

void test_fifo_init_malloc_fault_past_bound(void)
{
	struct fiber_queue_init_result res;
	struct fiber_fifo_jq *jq;

	alloc_fault_reset(malloc, free, QUEUE_FIFO_MALLOC_COUNT);

	res = fiber_queue_fifo_init(queue_length, alloc_fault_malloc,
				    alloc_fault_free);

	TEST_ASSERT_EQUAL(0, res.error);
	TEST_ASSERT_NOT_NULL(res.queue);
	jq = (struct fiber_fifo_jq *)res.queue;
	validate_queue(jq, queue_length, alloc_fault_free);
	fiber_queue_fifo_free(res.queue);
}

int main(void)
{
	UNITY_BEGIN();
	threading_trace_fault_init();
	alloc_fault_init();

	RUN_TEST(test_fifo_init_malloc_fault0);
	RUN_TEST(test_fifo_init_malloc_fault1);
	RUN_TEST(test_fifo_init_malloc_fault_past_bound);

	alloc_fault_destroy();
	threading_trace_fault_destroy();
	return UNITY_END();
}
