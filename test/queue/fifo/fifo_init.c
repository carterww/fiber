#include <stdlib.h>

#include "test/unity.h"
#include "test/mock/alloc/alloc_trace.h"

#include "fiber.h"
#include "fiber_fifo.h"
#include "fifo_validate.h"
#include "src/queue/fifo_internal.h"

static const qsize queue_length = 10;

void setUp(void)
{
	alloc_trace_reset(malloc, free);
}

void tearDown(void)
{
	alloc_trace_verify();
}

void test_fifo_init_valid(void)
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
	alloc_trace_init();

	RUN_TEST(test_fifo_init_valid);

	alloc_trace_destroy();
	return UNITY_END();
}
