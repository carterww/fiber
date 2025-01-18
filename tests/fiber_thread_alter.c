#include <time.h>
#include <unistd.h>

#include "fiber.h"
#include "src/fiber.c"
#include "fiber_fifo.h"
#include "xtal.h"

#define DEFAULT_THREADS_NUMBER 2
struct fiber_pool *pool = NULL;
struct fiber_queue_operations queue_ops = FIBER_FIFO_QUEUE_OPERATIONS;
struct fiber_pool_init_options default_opts = {
	.queue_ops = &queue_ops,
	.malloc = malloc,
	.free = free,
	.threads_number = DEFAULT_THREADS_NUMBER,
	.queue_length = 10,
};

void *sleep_n(void *arg)
{
	unsigned long sleep_time = (unsigned long)arg;
	sleep(sleep_time);
	return NULL;
}

struct fiber_job pseudo_job = { .job_id = 0,
				.job_arg = (void *)1,
				.job_func = sleep_n };

void setup(struct fiber_pool_init_options *opts);
void teardown();

TEST(threads_add_bad_args)
{
	setup(NULL);
	int res = fiber_threads_add(NULL, 5);
	ASSERT_EQUAL_INT(FBR_ENULL_ARGS, res);
	res = fiber_threads_add(pool, 0);
	ASSERT_EQUAL_INT(FBR_EINVLD_SIZE, res);
	res = fiber_threads_add(pool, -1);
	ASSERT_EQUAL_INT(FBR_EINVLD_SIZE, res);
	teardown();
}

TEST(threads_add)
{
	setup(NULL);
	int res = fiber_threads_add(pool, 2);
	ASSERT_EQUAL_INT(0, res);
	tpsize tn_curr = fiber_threads_number(pool);
	ASSERT_EQUAL_INT(DEFAULT_THREADS_NUMBER + 2, tn_curr);
	tpsize wk_num = fiber_threads_working(pool);
	ASSERT_EQUAL_INT(0, wk_num);
	res = fiber_threads_add(pool, 2);
	ASSERT_EQUAL_INT(0, res);
	tn_curr = fiber_threads_number(pool);
	ASSERT_EQUAL_INT(DEFAULT_THREADS_NUMBER + 4, tn_curr);
	teardown();
}

TEST(threads_remove_bad_args)
{
	setup(NULL);
	int res = fiber_threads_remove(NULL, 2);
	ASSERT_EQUAL_INT(FBR_ENULL_ARGS, res);
	res = fiber_threads_remove(pool, 0);
	ASSERT_EQUAL_INT(FBR_EINVLD_SIZE, res);
	res = fiber_threads_remove(pool, -1);
	ASSERT_EQUAL_INT(FBR_EINVLD_SIZE, res);
	teardown();
}

TEST(threads_remove_idle)
{
	setup(NULL);
	int res = fiber_threads_remove(pool, DEFAULT_THREADS_NUMBER);
	int poll_tries = 5;
	do {
		sleep(1);
	} while (fiber_threads_number(pool) != 0 && poll_tries-- > 0);
	if (poll_tries <= 0) {
		FAIL("Idle threads were never killed");
	}
	ASSERT_EQUAL_INT(0, pool->pool_flags);
	teardown();
}

TEST(threads_remove_working)
{
	setup(NULL);
	struct fiber_job sleep_3 = pseudo_job;
	sleep_3.job_arg = (void *)3;
	struct fiber_job sleep_4 = pseudo_job;
	sleep_4.job_arg = (void *)4;
	int res = fiber_job_push(pool, &sleep_3, FIBER_QUEUE_NO_BLOCK);
	if (res < 0) {
		FAIL("fiber_job_push error");
	}
	res = fiber_job_push(pool, &sleep_4, FIBER_QUEUE_NO_BLOCK);
	if (res < 0) {
		FAIL("fiber_job_push error");
	}
	// Give time for threads to pop jobs
	sleep(1);
	res = fiber_threads_remove(pool, DEFAULT_THREADS_NUMBER);
	ASSERT_EQUAL_INT(0, res);

	int poll_tries = 5;
	do {
		sleep(1);
	} while (fiber_threads_number(pool) != 0 && poll_tries-- > 0);
	if (poll_tries <= 0) {
		FAIL("Idle threads were never killed");
	}
	ASSERT_EQUAL_INT(0, pool->pool_flags);
	teardown();
}

int main()
{
	run_tests();
	return 0;
}

void setup(struct fiber_pool_init_options *opts)
{
	if (opts == NULL) {
		opts = &default_opts;
	}
	struct fiber_init_result res = fiber_init(opts);
	ASSERT_EQUAL_INT(0, res.error);
	ASSERT_NOT_NULL(res.pool)
	pool = res.pool;
	tpsize tn = fiber_threads_number(pool);
	ASSERT_EQUAL_INT(opts->threads_number, tn);
}

void teardown()
{
	fiber_free(pool);
}
