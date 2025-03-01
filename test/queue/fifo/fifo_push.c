#include "src/threading.h"
#include "test/queue/fifo/fifo_validate.h"
#include <stdlib.h>

#include "fiber.h"
#include "fiber_fifo.h"
#include "src/queue/fifo_internal.h"

#include "test/mock/alloc/alloc_trace.h"
#include "test/mock/threading/threading_trace_fault.h"
#include "test/unity.h"

static struct fiber_fifo_jq *jq = NULL;
static qsize queue_cap = 16;

static void verify_queue_state(qsize push_num)
{
	int sem_val;

	TEST_ASSERT_EQUAL(push_num % queue_cap, jq->tail);
	TEST_ASSERT_EQUAL(0, jq->head);

	TEST_ASSERT_EQUAL(0, fiber_sem_getvalue(&jq->void_num, &sem_val));
	TEST_ASSERT_EQUAL(queue_cap - push_num, sem_val);
	TEST_ASSERT_EQUAL(0, fiber_sem_getvalue(&jq->jobs_num, &sem_val));
	TEST_ASSERT_EQUAL(push_num, sem_val);
}

static void run_push_test_normal(unsigned long flags)
{
	struct fiber_job job;
	int res;

	job.job_id = 0x5a5a;
	res = fiber_queue_fifo_push(jq, &job, flags);
	TEST_ASSERT_EQUAL(0, res);
	verify_queue_state(1);

	job.job_id = 0xa5a5;
	res = fiber_queue_fifo_push(jq, &job, flags);
	TEST_ASSERT_EQUAL(0, res);
	verify_queue_state(2);

	TEST_ASSERT_EQUAL(0x5a5a, jq->jobs[0].job_id);
	TEST_ASSERT_EQUAL(0xa5a5, jq->jobs[1].job_id);
}

void setUp(void)
{
	struct fiber_queue_init_result res;

	threading_trace_fault_reset();
	alloc_trace_reset(malloc, free);
	res = fiber_queue_fifo_init(queue_cap, alloc_trace_malloc,
				    alloc_trace_free);
	TEST_ASSERT_EQUAL_MESSAGE(0, res.error,
				  "failed to initialized fifo in setUp");
	TEST_ASSERT_NOT_NULL(res.queue);
	jq = res.queue;
}

void tearDown(void)
{
	if (jq != NULL) {
		fiber_queue_fifo_free((void *)jq);
		jq = NULL;
	}
	alloc_trace_verify();
	threading_trace_fault_verify();
}

void test_fifo_push_queue_block_normal(void)
{
	run_push_test_normal(FIBER_QUEUE_BLOCK);
}

void test_fifo_push_queue_no_block_normal(void)
{
	run_push_test_normal(FIBER_QUEUE_NO_BLOCK);
}

void test_fifo_push_queue_no_block_full(void)
{
	struct fiber_job job;
	int res;
	qsize i;

	for (i = 0; i < queue_cap; ++i) {
		job.job_id = i;
		res = fiber_queue_fifo_push(jq, &job, FIBER_QUEUE_NO_BLOCK);
		TEST_ASSERT_EQUAL(0, res);
	}
	job.job_id = queue_cap + 1;
	res = fiber_queue_fifo_push(jq, &job, FIBER_QUEUE_NO_BLOCK);
	TEST_ASSERT_EQUAL(FBR_EAGAIN, res);
	verify_queue_state(queue_cap);
	for (i = 0; i < queue_cap; ++i) {
		TEST_ASSERT_NOT_EQUAL(jq->jobs[i].job_id, queue_cap + 1);
	}
}

int main(void)
{
	UNITY_BEGIN();
	threading_trace_fault_init();
	alloc_trace_init();

	RUN_TEST(test_fifo_push_queue_block_normal);
	RUN_TEST(test_fifo_push_queue_no_block_normal);
	RUN_TEST(test_fifo_push_queue_no_block_full);

	alloc_trace_destroy();
	threading_trace_fault_destroy();
	return UNITY_END();
}
