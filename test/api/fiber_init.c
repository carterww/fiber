#include <limits.h>
#include <stdlib.h>

#include "fiber/fiber.h"
#include "fiber/fiber_fifo.h"
#include "fiber_atomic/atomic.h"
#include "src/fiber_internal.h"

#include "test/busy_wait.h"
#include "test/mock/alloc/alloc_trace.h"
#include "test/mock/threading/threading_trace_fault.h"
#include "test/unity.h"

/* All FBR_E* errors are negative. */
#define FIBER_INIT_INVALID_ERROR (20)

/* Put invalid values in fiber_init_result to ensure fiber_init changes them */
#define INIT_RES(res)                                 \
	do {                                          \
		res.error = FIBER_INIT_INVALID_ERROR; \
		res.pool = (struct fiber_pool *)0x4;  \
	} while (0)

static struct fiber_queue_operations fifo_qops = FIBER_FIFO_QUEUE_OPERATIONS;

static const struct fiber_pool_init_options pool_base_options = {
	NULL,
	alloc_trace_malloc,
	alloc_trace_free,
	FIBER_THREADS_NUMBER_INIT_MIN,
	FIBER_QUEUE_LENGTH_INIT_MIN,
};

static void init_opts_valid(struct fiber_pool_init_options *opts,
			    struct fiber_queue_operations *qops)
{
	*opts = pool_base_options;
	*qops = fifo_qops;

	opts->queue_ops = qops;
}

/* Validates members of pool based on the options in opts */
static void validate_pool(struct fiber_pool *pool,
			  struct fiber_pool_init_options *opts)
{
	tpsize curr_threads_number;
	unsigned long i = 0;

	TEST_ASSERT_NOT_NULL(pool);

	TEST_ASSERT_EQUAL(-1, pool->job_id_prev);
	do {
		curr_threads_number = fiber_atomic_load(&pool->threads_number,
							FIBER_ATOMIC_ACQUIRE);
		if (curr_threads_number == opts->threads_number) {
			break;
		}
		mssleep_busy_wait(50);
	} while (i++ < 30);
	/* Wait at most 1.5 seconds for the value to be correct */
	TEST_ASSERT_EQUAL(opts->threads_number, curr_threads_number);
	TEST_ASSERT_EQUAL(0, pool->threads_working);
	TEST_ASSERT_EQUAL(0, pool->threads_kill_number);
	TEST_ASSERT_EQUAL(opts->malloc, pool->malloc);
	TEST_ASSERT_EQUAL(opts->free, pool->free);

	TEST_ASSERT_NOT_NULL(pool->job_queue);
	if (opts->threads_number == 0) {
		TEST_ASSERT_NULL(pool->thread_head);
	} else {
		TEST_ASSERT_NOT_NULL(pool->thread_head);
	}

	TEST_ASSERT_EQUAL(opts->queue_ops->push, pool->queue_ops.push);
	TEST_ASSERT_EQUAL(opts->queue_ops->pop, pool->queue_ops.pop);
	TEST_ASSERT_EQUAL(opts->queue_ops->init, pool->queue_ops.init);
	TEST_ASSERT_EQUAL(opts->queue_ops->free, pool->queue_ops.free);
	TEST_ASSERT_EQUAL(opts->queue_ops->length, pool->queue_ops.length);
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
	alloc_trace_reset(malloc, free);
	threading_trace_fault_reset();
}

void tearDown(void)
{
	alloc_trace_verify();
	threading_trace_fault_verify();
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
	test_invalid_length_runner(FIBER_THREADS_NUMBER_INIT_MIN - 1,
				   FIBER_QUEUE_LENGTH_INIT_MIN);
}

void test_fiber_init_opts_queue_length_invalid(void)
{
	/* min - 1 will never cause underflow because qsize is always
         * signed and min should never be negative.
         */
	test_invalid_length_runner(FIBER_THREADS_NUMBER_INIT_MIN,
				   FIBER_QUEUE_LENGTH_INIT_MIN - 1);
}

void test_fiber_init_opts_alloc_null(void)
{
	struct fiber_init_result res;
	struct fiber_pool_init_options opts;
	struct fiber_queue_operations qops;

	INIT_RES(res);
	init_opts_valid(&opts, &qops);

	opts.malloc = NULL;
	opts.free = alloc_trace_free;
	res = fiber_init(&opts);
	TEST_ASSERT_EQUAL(FBR_ENO_ALLOC, res.error);
	TEST_ASSERT_NULL(res.pool);

	INIT_RES(res);

	opts.malloc = alloc_trace_malloc;
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

void test_fiber_init_valid_no_threads(void)
{
	struct fiber_init_result res;
	struct fiber_pool_init_options opts;
	struct fiber_queue_operations qops;

	INIT_RES(res);
	init_opts_valid(&opts, &qops);

	opts.threads_number = 0;

	res = fiber_init(&opts);
	TEST_ASSERT_EQUAL(0, res.error);
	validate_pool(res.pool, &opts);

	fiber_free(res.pool);
}

void test_fiber_init_valid_threads(void)
{
	struct fiber_init_result res;
	struct fiber_pool_init_options opts;
	struct fiber_queue_operations qops;

	INIT_RES(res);
	init_opts_valid(&opts, &qops);

	opts.threads_number = 1;

	res = fiber_init(&opts);
	TEST_ASSERT_EQUAL(0, res.error);
	validate_pool(res.pool, &opts);

	fiber_free(res.pool);
}

int main(void)
{
	UNITY_BEGIN();

	threading_trace_fault_init();
	alloc_trace_init();

	RUN_TEST(test_fiber_init_opts_null);
	RUN_TEST(test_fiber_init_opts_threads_number_invalid);
	RUN_TEST(test_fiber_init_opts_queue_length_invalid);
	RUN_TEST(test_fiber_init_opts_alloc_null);
	RUN_TEST(test_fiber_init_queue_ops_null);
	RUN_TEST(test_fiber_init_queue_ops_func_ptrs_null);
	RUN_TEST(test_fiber_init_valid_no_threads);
	RUN_TEST(test_fiber_init_valid_threads);

	alloc_trace_destroy();
	threading_trace_fault_destroy();

	return UNITY_END();
}

#undef INIT_RES
