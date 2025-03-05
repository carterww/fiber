#include <stdlib.h>

#include "fiber.h"
#include "fiber_fifo.h"
#include "src/queue/fifo_internal.h"
#include "src/threading.h"

#include "test/mock/alloc/alloc_trace.h"
#include "test/mock/threading/threading_trace_fault.h"
#include "test/unity.h"

static struct fiber_fifo_jq *jq = NULL;
static qsize queue_cap = 16;

static void *fake_runner(void *arg)
{
	(void)arg;
	return NULL;
}

static void verify_queue_state(qsize push_num, qsize pop_num)
{
	int sem_val;

	TEST_ASSERT_EQUAL(push_num % queue_cap, jq->tail);
	TEST_ASSERT_EQUAL(pop_num % queue_cap, jq->head);

	TEST_ASSERT_EQUAL(0, fiber_sem_getvalue(&jq->void_num, &sem_val));
	TEST_ASSERT_EQUAL(queue_cap - push_num + pop_num, sem_val);
	TEST_ASSERT_EQUAL(0, fiber_sem_getvalue(&jq->jobs_num, &sem_val));
	TEST_ASSERT_EQUAL(push_num - pop_num, sem_val);
}

static void run_pop_test_normal(unsigned long flags)
{
	struct fiber_job job;
	int res, i;

	for (i = 0; i < queue_cap; ++i) {
		job.job_id = 0x5a5a;
		job.job_func = fake_runner;
		job.job_arg = NULL;
		res = fiber_queue_fifo_push(jq, &job, flags);
		TEST_ASSERT_EQUAL(0, res);
		verify_queue_state(i + 1, 0);
	}
	for (i = 0; i < queue_cap; ++i) {
		res = fiber_queue_fifo_pop(jq, &job, flags);
		TEST_ASSERT_EQUAL(0, res);
		verify_queue_state(queue_cap, i + 1);
		TEST_ASSERT_EQUAL(0x5a5a, jq->jobs[i].job_id);
		TEST_ASSERT_EQUAL(fake_runner, job.job_func);
		TEST_ASSERT_NULL(job.job_arg);
	}
	res = fiber_queue_fifo_pop(jq, &job, FIBER_QUEUE_NO_BLOCK);
	TEST_ASSERT_EQUAL(FBR_EAGAIN, res);
	verify_queue_state(queue_cap, queue_cap);
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

void test_fifo_pop_queue_block_normal(void)
{
	run_pop_test_normal(FIBER_QUEUE_BLOCK);
}

void test_fifo_pop_queue_no_block_normal(void)
{
	run_pop_test_normal(FIBER_QUEUE_NO_BLOCK);
}

void test_fifo_pop_queue_wrap(void)
{
	struct fiber_job job;
	int res;
	qsize i;

	for (i = 0; i < queue_cap; ++i) {
		job.job_id = i;
		res = fiber_queue_fifo_push(jq, &job, FIBER_QUEUE_NO_BLOCK);
		TEST_ASSERT_EQUAL(0, res);
		TEST_ASSERT_EQUAL(i, job.job_id);
	}
	verify_queue_state(queue_cap, 0);
	res = fiber_queue_fifo_pop(jq, &job, FIBER_QUEUE_BLOCK);
	TEST_ASSERT_EQUAL(0, res);
	TEST_ASSERT_EQUAL(0, job.job_id); /* Should be first job pushed */
	verify_queue_state(queue_cap, 1);
	job.job_id = queue_cap + 1;
	res = fiber_queue_fifo_push(jq, &job, FIBER_QUEUE_BLOCK);
	TEST_ASSERT_EQUAL(0, res);
	verify_queue_state(queue_cap + 1, 1);
}

void test_fifo_pop_queue_correct_order(void)
{
	struct fiber_job job;
	int res;
	qsize i;

	for (i = 0; i < queue_cap; ++i) {
		job.job_id = i;
		res = fiber_queue_fifo_push(jq, &job,
					    i % 2 == 0 ? FIBER_QUEUE_BLOCK :
							 FIBER_QUEUE_NO_BLOCK);
		TEST_ASSERT_EQUAL(0, res);
		TEST_ASSERT_EQUAL(i, job.job_id);
	}
	verify_queue_state(queue_cap, 0);
	for (i = 0; i < queue_cap; ++i) {
		res = fiber_queue_fifo_pop(jq, &job,
					   i % 2 == 0 ? FIBER_QUEUE_NO_BLOCK :
							FIBER_QUEUE_BLOCK);
		TEST_ASSERT_EQUAL(0, res);
		TEST_ASSERT_EQUAL(i, job.job_id);
	}
	verify_queue_state(queue_cap, queue_cap);
}

void test_fifo_pop_queue_multiwrap(void)
{
	struct fiber_job job;
	int res;
	qsize i;

	for (i = 0; i < queue_cap * 12; ++i) {
		job.job_id = i;
		res = fiber_queue_fifo_push(jq, &job, FIBER_QUEUE_BLOCK);
		TEST_ASSERT_EQUAL(0, res);
		TEST_ASSERT_EQUAL(i, job.job_id);
		verify_queue_state(i + 1, i);
		res = fiber_queue_fifo_pop(jq, &job, FIBER_QUEUE_BLOCK);
		TEST_ASSERT_EQUAL(0, res);
		TEST_ASSERT_EQUAL(i, job.job_id);
		verify_queue_state(i + 1, i + 1);
	}
	verify_queue_state(queue_cap * 12, queue_cap * 12);
}

int main(void)
{
	UNITY_BEGIN();
	threading_trace_fault_init();
	alloc_trace_init();

	RUN_TEST(test_fifo_pop_queue_block_normal);
	RUN_TEST(test_fifo_pop_queue_no_block_normal);
	RUN_TEST(test_fifo_pop_queue_wrap);
	RUN_TEST(test_fifo_pop_queue_correct_order);
	RUN_TEST(test_fifo_pop_queue_multiwrap);

	alloc_trace_destroy();
	threading_trace_fault_destroy();
	return UNITY_END();
}
