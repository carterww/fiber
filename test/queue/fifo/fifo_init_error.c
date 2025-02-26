#include "test/unity.h"
#include "test/mock/alloc/alloc_fault.h"

#include "fiber.h"
#include "fiber_fifo.h"
#include "fifo_validate.h"
#include "src/queue/fifo_internal.h"
#include "test/unity_internals.h"

#define QUEUE_FIFO_MALLOC_COUNT (2)

#define test_fifo_init_malloc_fault_body(normal_malloc_count)                 \
	do {                                                                  \
		struct fiber_queue_init_result res;                           \
		struct fiber_fifo_jq *jq;                                     \
                                                                              \
		alloc_fault_reset(normal_malloc_count);                       \
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
}

void tearDown(void)
{
	alloc_fault_verify();
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

	alloc_fault_reset(QUEUE_FIFO_MALLOC_COUNT);

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

	RUN_TEST(test_fifo_init_malloc_fault0);
	RUN_TEST(test_fifo_init_malloc_fault1);
	RUN_TEST(test_fifo_init_malloc_fault_past_bound);

	return UNITY_END();
}
