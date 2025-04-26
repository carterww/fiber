#include <stdlib.h>

#include "fiber/fiber.h"
#include "fiber_atomic/atomic.h"
#include "src/twql_packed.h"

#include "test/unity.h"

#define TW_INITIAL (0)
#define QL_INITIAL (0)

static union fiber_twql_packed p;

void setUp(void)
{
	p.counters.threads_working = TW_INITIAL;
	p.counters.queue_length = QL_INITIAL;

	TEST_ASSERT_EQUAL(TW_INITIAL, p.counters.threads_working);
	TEST_ASSERT_EQUAL(QL_INITIAL, p.counters.queue_length);
}

void tearDown(void)
{
}

void test_twql_packed_load(void)
{
	struct fiber_twql counters;

	counters = fiber_twql_load(&p, FIBER_ATOMIC_RELAXED);

	TEST_ASSERT_EQUAL(TW_INITIAL, counters.threads_working);
	TEST_ASSERT_EQUAL(QL_INITIAL, counters.queue_length);

	fiber_twql_add(&p, -10, -10);
	counters = fiber_twql_load(&p, FIBER_ATOMIC_RELAXED);

	TEST_ASSERT_EQUAL(TW_INITIAL - 10, counters.threads_working);
	TEST_ASSERT_EQUAL(QL_INITIAL - 10, counters.queue_length);
}

void test_twql_packed_add(void)
{
	tpsize tw_add_seq[5] = { 1, -1, 1, -1, 0 };
	qsize ql_add_seq[5] = { 1, 1, -1, -1, 0 };
	tpsize tw_tracker = TW_INITIAL;
	qsize ql_tracker = QL_INITIAL;
	unsigned long i;

	for (i = 0; i < sizeof(tw_add_seq) / sizeof(*tw_add_seq); ++i) {
		tw_tracker += tw_add_seq[i];
		ql_tracker += ql_add_seq[i];
		fiber_twql_add(&p, tw_add_seq[i], ql_add_seq[i]);
		TEST_ASSERT_EQUAL(tw_tracker, p.counters.threads_working);
		TEST_ASSERT_EQUAL(ql_tracker, p.counters.queue_length);
	}
}

void test_twql_packed_queue_length_add(void)
{
	qsize ql_add_seq[3] = { 1, -1, 0 };
	qsize ql_tracker = QL_INITIAL;
	unsigned long i;

	for (i = 0; i < sizeof(ql_add_seq) / sizeof(*ql_add_seq); ++i) {
		ql_tracker += ql_add_seq[i];
		fiber_twql_add(&p, 0, ql_add_seq[i]);
		TEST_ASSERT_EQUAL(TW_INITIAL, p.counters.threads_working);
		TEST_ASSERT_EQUAL(ql_tracker, p.counters.queue_length);
	}
}

void test_twql_packed_thread_working_add(void)
{
	tpsize tw_add_seq[3] = { 1, -1, 0 };
	tpsize tw_tracker = TW_INITIAL;
	unsigned long i;

	for (i = 0; i < sizeof(tw_add_seq) / sizeof(*tw_add_seq); ++i) {
		tw_tracker += tw_add_seq[i];
		fiber_twql_add(&p, tw_add_seq[i], 0);
		TEST_ASSERT_EQUAL(tw_tracker, p.counters.threads_working);
		TEST_ASSERT_EQUAL(QL_INITIAL, p.counters.queue_length);
	}
}

int main(void)
{
	UNITY_BEGIN();

	RUN_TEST(test_twql_packed_load);
	RUN_TEST(test_twql_packed_add);
	RUN_TEST(test_twql_packed_queue_length_add);
	RUN_TEST(test_twql_packed_thread_working_add);

	return UNITY_END();
}
