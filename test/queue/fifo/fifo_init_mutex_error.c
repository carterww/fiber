#include <stdlib.h>

#include "fiber/fiber.h"
#include "fiber/fiber_fifo.h"
#include "fifo_validate.h"

#include "test/mock/alloc/alloc_trace.h"
#include "test/mock/threading/threading_trace_fault.h"
#include "test/unity.h"

#define QUEUE_FIFO_MUTEX_COUNT (2)

#define test_fifo_init_mutex_fault_body(fail_after, expected_error)           \
	do {                                                                  \
		struct fiber_queue_init_result res;                           \
		struct fiber_fifo_jq *jq;                                     \
		struct threading_trace_fault_mutex_control_components         \
			*mtx_ctrl;                                            \
                                                                              \
		mtx_ctrl = threading_trace_fault_mutex_get();                 \
		mtx_ctrl->init.count = fail_after;                            \
		mtx_ctrl->init.fail_res = expected_error;                     \
                                                                              \
		res = fiber_queue_fifo_init(queue_length, alloc_trace_malloc, \
					    alloc_trace_free);                \
                                                                              \
		TEST_ASSERT_EQUAL(expected_error, res.error);                 \
		TEST_ASSERT_NULL(res.queue);                                  \
	} while (0)

static const qsize queue_length = 10;

void setUp(void)
{
	threading_trace_fault_reset();
	alloc_trace_reset(malloc, free);
}

void tearDown(void)
{
	alloc_trace_verify();
	threading_trace_fault_verify();
}

/* mutex_init can return 1 of 3 errors but handles all of them the same
 * way: passing it to caller. For now, only need to test with one error.
 */
void test_fifo_init_mutex_fault0_semrng(void)
{
	test_fifo_init_mutex_fault_body(0, FBR_ENOMEM);
}
void test_fifo_init_mutex_fault1_semrng(void)
{
	test_fifo_init_mutex_fault_body(1, FBR_ENOMEM);
}

void test_fifo_init_sem_fault_past_bound(void)
{
	struct fiber_queue_init_result res;
	struct fiber_fifo_jq *jq;

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

	RUN_TEST(test_fifo_init_mutex_fault0_semrng);
	RUN_TEST(test_fifo_init_mutex_fault1_semrng);
	RUN_TEST(test_fifo_init_sem_fault_past_bound);

	alloc_trace_destroy();
	threading_trace_fault_destroy();
	return UNITY_END();
}
