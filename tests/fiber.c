#include "../fiber.c"
#include "xtal.h"

#define DEFAULT_THREADS_NUMBER 2
struct fiber_pool pool;
struct fiber_pool_init_options default_opts = {
	.queue_ops = NULL, // Use default FIFO
	.malloc = malloc,
	.free = free,
	.threads_number = DEFAULT_THREADS_NUMBER,
	.queue_length = 10,
};

void setup(struct fiber_pool_init_options *opts);
void teardown();

#define ASSERT_EQUAL_JID(expected, actual) ASSERT_EQUAL_LONG(expected, actual)

#if JOB_ID_MAX < INT64_MAX || defined(FIBER_CHECK_JID_OVERFLOW)
TEST(job_id_overflow)
{
	jid almost_max = JOB_ID_MAX - 1;
	jid max_jid = get_and_update_jid(&almost_max);
	ASSERT_EQUAL_JID(JOB_ID_MAX, max_jid);
	jid wrapped = get_and_update_jid(&almost_max);
	ASSERT_EQUAL_JID((jid)0, wrapped);
}
#endif

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
