#include <stdlib.h>

#include "fiber/fiber.h"

#include "test/mock/alloc/alloc_trace.h"
#include "test/mock/threading/threading_trace_fault.h"
#include "test/queue_implementations.h"
#include "test/unity.h"

struct fiber_pool_init_options init_opts = { NULL, alloc_trace_malloc,
					     alloc_trace_free, 0, 16 };

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

void test_fiber_job_push_raw_null_args_runner(struct fiber_pool *pool)
{
	jid job_id;
	struct fiber_job job;

	job.job_func = fake_job_func;
	job.job_arg = NULL;

	job_id = fiber_job_push_raw(NULL, &job, 0);
	TEST_ASSERT_EQUAL(FBR_ENULL_ARGS, job_id);

	job_id = fiber_job_push_raw(pool, NULL, 0);
	TEST_ASSERT_EQUAL(FBR_ENULL_ARGS, job_id);

	job.job_func = NULL;
	job_id = fiber_job_push_raw(pool, &job, 0);
	TEST_ASSERT_EQUAL(FBR_ENULL_ARGS, job_id);
}

void test_fiber_job_push_raw_null_args(void)
{
	test_queue_implementations_all(
		&init_opts, test_fiber_job_push_raw_null_args_runner);
}

void test_fiber_job_push_raw_normal_runner(struct fiber_pool *pool)
{
	jid job_id;
	struct fiber_job job;

	job.job_id = 1024;
	job.job_func = fake_job_func;
	job.job_arg = NULL;

	job_id = fiber_job_push_raw(pool, &job, 0);
	TEST_ASSERT_EQUAL(job.job_id, job_id);
}

void test_fiber_job_push_raw_normal(void)
{
	test_queue_implementations_all(&init_opts,
				       test_fiber_job_push_raw_normal_runner);
}

int main(void)
{
	UNITY_BEGIN();

	threading_trace_fault_init();
	alloc_trace_init();

	RUN_TEST(test_fiber_job_push_raw_null_args);
	RUN_TEST(test_fiber_job_push_raw_normal);

	alloc_trace_destroy();
	threading_trace_fault_destroy();

	return UNITY_END();
}
