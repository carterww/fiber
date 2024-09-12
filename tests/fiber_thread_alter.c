#include "../fiber.c"
#include "xtal.h"
#include <time.h>
#include <unistd.h>

#define DEFAULT_THREADS_NUMBER 2
struct fiber_pool pool = { 0 };
struct fiber_pool_init_options default_opts = {
	.queue_ops = NULL, // Use default FIFO
	.malloc = malloc,
	.free = free,
	.threads_number = DEFAULT_THREADS_NUMBER,
	.queue_length = 10,
};

void setup(struct fiber_pool_init_options *opts);
void teardown();

TEST(threads_add_bad_args)
{
	int res = fiber_threads_add(NULL, 5);
	ASSERT_EQUAL_INT(FBR_ENULL_ARGS, res);
	res = fiber_threads_add(&pool, 0);
	ASSERT_EQUAL_INT(FBR_EINVLD_SIZE, res);
	res = fiber_threads_add(&pool, -1);
	ASSERT_EQUAL_INT(FBR_EINVLD_SIZE, res);
}

TEST(threads_add)
{
	setup(NULL);
	tpsize tn_orig = fiber_threads_number(&pool);
	ASSERT_EQUAL_INT(DEFAULT_THREADS_NUMBER, tn_orig);
	int res = fiber_threads_add(&pool, 2);
	ASSERT_EQUAL_INT(0, res);
	tpsize tn_curr = fiber_threads_number(&pool);
	ASSERT_EQUAL_INT(tn_orig + 2, tn_curr);
	tpsize wk_num = fiber_threads_working(&pool);
	ASSERT_EQUAL_INT(0, wk_num);
	res = fiber_threads_add(&pool, 2);
	ASSERT_EQUAL_INT(0, res);
	tn_curr = fiber_threads_number(&pool);
	ASSERT_EQUAL_INT(tn_orig + 4, tn_curr);
	teardown();
}

TEST(threads_remove_bad_args)
{
	int res = fiber_threads_remove(NULL, 2);
	ASSERT_EQUAL_INT(FBR_ENULL_ARGS, res);
	res = fiber_threads_remove(&pool, 0);
	ASSERT_EQUAL_INT(FBR_EINVLD_SIZE, res);
	res = fiber_threads_remove(&pool, -1);
	ASSERT_EQUAL_INT(FBR_EINVLD_SIZE, res);
	res = fiber_threads_remove(&pool, 2);
	ASSERT_EQUAL_INT(FBR_EPOOL_UNINIT, res);
}

TEST(threads_remove_idle)
{
	setup(NULL);
	tpsize tn_orig = fiber_threads_number(&pool);
	ASSERT_EQUAL_INT(DEFAULT_THREADS_NUMBER, tn_orig);
	int res = fiber_threads_remove(&pool, DEFAULT_THREADS_NUMBER);
	int poll_tries = 5;
	do {
		sleep(1);
	} while (fiber_threads_number(&pool) != 0 && poll_tries-- > 0);
	if (poll_tries <= 0) {
		FAIL("Idle threads were never killed");
	}
	teardown();
}

TEST(threads_remove_working)
{
}

TEST(threads_remove_mix)
{
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
	int res = fiber_init(&pool, opts);
	ASSERT_EQUAL_INT(0, res);
}

void teardown()
{
	fiber_free(&pool);
}
