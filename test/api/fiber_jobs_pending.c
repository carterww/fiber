#include <stdlib.h>

#include "fiber.h"

#include "test/mock/alloc/alloc_trace.h"
#include "test/mock/threading/threading_trace_fault.h"
#include "test/queue_implementations.h"
#include "test/unity.h"

#define QUEUE_CAP (16)

#define QUEUE_OPERATIONS_ALL_LOOP(i)                                           \
	for (i = 0;                                                            \
	     i < sizeof(queue_operations_all) / sizeof(*queue_operations_all); \
	     ++i)

struct fiber_pool_init_options init_opts = { NULL, alloc_trace_malloc,
					     alloc_trace_free, 0, QUEUE_CAP };

static void *fake_job_func(void *arg)
{
	(void)arg;
	return NULL;
}

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

void test_fiber_jobs_pending_null_arg(void)
{
	qsize jobs;

	jobs = fiber_jobs_pending(NULL);
	TEST_ASSERT_EQUAL(FBR_ENULL_ARGS, jobs);
}

void test_fiber_jobs_pending_no_jobs_runner(struct fiber_pool *pool)
{
	qsize jobs;
	jobs = fiber_jobs_pending(pool);
	TEST_ASSERT_EQUAL(0, jobs);
}

void test_fiber_jobs_pending_no_jobs(void)
{
	test_queue_implementations_all(&init_opts,
				       test_fiber_jobs_pending_no_jobs_runner);
}

void test_fiber_jobs_pending_no_threads_runner(struct fiber_pool *pool)
{
	struct fiber_job job;
	qsize jobs, i;
	jid push_res;

	for (i = 0; i < init_opts.queue_length; ++i) {
		job.job_func = fake_job_func;
		job.job_arg = NULL;
		push_res = fiber_job_push(pool, &job, 0);
		TEST_ASSERT_GREATER_OR_EQUAL(0, push_res);

		jobs = fiber_jobs_pending(pool);
		TEST_ASSERT_EQUAL(i + 1, jobs);
	}
}

void test_fiber_jobs_pending_no_threads(void)
{
	test_queue_implementations_all(
		&init_opts, test_fiber_jobs_pending_no_threads_runner);
}

int main(void)
{
	UNITY_BEGIN();

	threading_trace_fault_init();
	alloc_trace_init();

	RUN_TEST(test_fiber_jobs_pending_no_jobs);
	RUN_TEST(test_fiber_jobs_pending_null_arg);
	RUN_TEST(test_fiber_jobs_pending_no_threads);

	alloc_trace_destroy();
	threading_trace_fault_destroy();

	return UNITY_END();
}
