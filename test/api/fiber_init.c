#include <stdlib.h>

#include "test/unity.h"

#include "fiber.h"
#include "src/fiber_internal.h"
#include "test/mock/queue/queue_noop.h"

/* All FBR_E* errors are negative. */
#define FIBER_INIT_INVALID_ERROR (20)

/* Put invalid values in fiber_init_result to ensure fiber_init changes them */
#define INIT_RES(res)                                 \
	do {                                          \
		res.error = FIBER_INIT_INVALID_ERROR; \
		res.pool = (struct fiber_pool *)0x4;  \
	} while (0)

static const struct fiber_pool_init_options pool_base_options = {
	NULL, malloc, free, FIBER_THREADS_NUMBER_MIN, FIBER_QUEUE_LENGTH_MIN,
};

static void init_opts_valid(struct fiber_pool_init_options *opts,
			    struct fiber_queue_operations *qops)
{
	*opts = pool_base_options;
	*qops = mock_queue_noop_operations;

	opts->queue_ops = qops;
}

static void test_invalid_length_runner(tpsize threads_number,
				       qsize queue_length)
{
	struct fiber_init_result res;
	struct fiber_pool_init_options opts;
	struct fiber_queue_operations qops;

	INIT_RES(res);
	init_opts_valid(&opts, &qops);

	opts.threads_number = threads_number;
	opts.queue_length = queue_length;

	res = fiber_init(&opts);
	TEST_ASSERT_EQUAL(FBR_EINVLD_SIZE, res.error);
	TEST_ASSERT_NULL(res.pool);
}

void setUp(void)
{
}

void tearDown(void)
{
}

void test_fiber_init_opts_null(void)
{
	struct fiber_init_result res;

	INIT_RES(res);

	res = fiber_init(NULL);
	TEST_ASSERT_EQUAL(FBR_ENULL_ARGS, res.error);
	TEST_ASSERT_NULL(res.pool);
}

void test_fiber_init_opts_threads_number_invalid(void)
{
	/* min - 1 will never cause underflow because tpsize is always
         * signed and min should never be negative.
         */
	test_invalid_length_runner(FIBER_THREADS_NUMBER_MIN - 1,
				   FIBER_QUEUE_LENGTH_MIN);
}

void test_fiber_init_opts_queue_length_invalid(void)
{
	/* min - 1 will never cause underflow because qsize is always
         * signed and min should never be negative.
         */
	test_invalid_length_runner(FIBER_THREADS_NUMBER_MIN,
				   FIBER_QUEUE_LENGTH_MIN - 1);
}

void test_fiber_init_opts_allocs_null(void)
{
	struct fiber_init_result res;
	struct fiber_pool_init_options opts;
	struct fiber_queue_operations qops;

	INIT_RES(res);
	init_opts_valid(&opts, &qops);

	opts.malloc = NULL;
	opts.free = free;
	res = fiber_init(&opts);
	TEST_ASSERT_EQUAL(FBR_ENO_ALLOC, res.error);
	TEST_ASSERT_NULL(res.pool);

	INIT_RES(res);

	opts.malloc = malloc;
	opts.free = NULL;
	res = fiber_init(&opts);
	TEST_ASSERT_EQUAL(FBR_ENO_ALLOC, res.error);
	TEST_ASSERT_NULL(res.pool);
}

void test_fiber_init_queue_ops_null(void)
{
	struct fiber_init_result res;
	struct fiber_pool_init_options opts;
	struct fiber_queue_operations qops;

	INIT_RES(res);
	init_opts_valid(&opts, &qops);

	opts.queue_ops = NULL;
	res = fiber_init(&opts);
	TEST_ASSERT_EQUAL(FBR_EQUEOPS_NONE, res.error);
	TEST_ASSERT_NULL(res.pool);
}

void test_fiber_init_queue_ops_func_ptrs_null(void)
{
	struct fiber_init_result res;
	struct fiber_pool_init_options opts;
	struct fiber_queue_operations qops;

	INIT_RES(res);
	init_opts_valid(&opts, &qops);

	opts.queue_ops->push = NULL;
	res = fiber_init(&opts);
	TEST_ASSERT_EQUAL(FBR_EQUEOPS_NONE, res.error);
	TEST_ASSERT_NULL(res.pool);
}

void test_fiber_init_valid(void)
{
	struct fiber_init_result res;
	struct fiber_pool_init_options opts;
	struct fiber_queue_operations qops;

	INIT_RES(res);
	init_opts_valid(&opts, &qops);

	res = fiber_init(&opts);
	TEST_ASSERT_EQUAL(0, res.error);
	TEST_ASSERT_NOT_NULL(res.pool);

	TEST_ASSERT_EQUAL(-1, res.pool->job_id_prev);
	TEST_ASSERT_EQUAL(opts.threads_number, res.pool->threads_number);
	TEST_ASSERT_EQUAL(0, res.pool->threads_working);
	TEST_ASSERT_EQUAL(0, res.pool->threads_kill_number);
	TEST_ASSERT_EQUAL(0, res.pool->pool_flags);
	TEST_ASSERT_EQUAL(opts.malloc, res.pool->malloc);
	TEST_ASSERT_EQUAL(opts.free, res.pool->free);

	TEST_ASSERT_NOT_NULL(res.pool->queue_ops);
	TEST_ASSERT_NOT_NULL(res.pool->job_queue);
	TEST_ASSERT_NOT_NULL(res.pool->thread_head);

	TEST_ASSERT_EQUAL(opts.queue_ops->push, res.pool->queue_ops->push);
	TEST_ASSERT_EQUAL(opts.queue_ops->pop, res.pool->queue_ops->pop);
	TEST_ASSERT_EQUAL(opts.queue_ops->init, res.pool->queue_ops->init);
	TEST_ASSERT_EQUAL(opts.queue_ops->free, res.pool->queue_ops->free);
	TEST_ASSERT_EQUAL(opts.queue_ops->length, res.pool->queue_ops->length);

	fiber_free(res.pool);
}

int main(void)
{
	UNITY_BEGIN();

	RUN_TEST(test_fiber_init_opts_null);
	RUN_TEST(test_fiber_init_opts_threads_number_invalid);
	RUN_TEST(test_fiber_init_opts_queue_length_invalid);
	RUN_TEST(test_fiber_init_opts_allocs_null);
	RUN_TEST(test_fiber_init_queue_ops_null);
	RUN_TEST(test_fiber_init_queue_ops_func_ptrs_null);
	RUN_TEST(test_fiber_init_valid);

	return UNITY_END();
}

#undef INIT_RES
