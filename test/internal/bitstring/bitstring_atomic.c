#include "fiber_atomic/atomic.h"
#include "fiber_lock/semaphore.h"
#include "src/bitstring.h"
#include "src/threading.h"

#include "test/unity.h"

#define BS_WORD_NUM (8)
#define BS_BIT_NUM (BS_WORD_NUM * FIBER_BITSTRING_BITS_PER_WORD)

#define NUM_SET_THREADS (2)
#define NUM_SET_ITERATIONS (10000)

struct test_bitstring_atomic_set_multi_threaded_arg {
	fiber_semaphore *start_sem;
	fiber_semaphore *end_sem;
};

static const size_t word_size_bytes = sizeof(fiber_bitstring_word);
static const size_t word_size_bits =
	word_size_bytes * FIBER_PLATFORM_BITS_PER_BYTE;
static const size_t bits_word_num_test_max = word_size_bits * 3 + 1;

static fiber_bitstring_word bs_words[BS_WORD_NUM];
static struct fiber_bitstring bitstring = { BS_WORD_NUM, bs_words };

void setUp(void)
{
	size_t i;
	for (i = 0; i < BS_WORD_NUM; ++i) {
		bs_words[i] = 0;
	}
	bitstring.word_num = BS_WORD_NUM;
	bitstring.words = bs_words;
}

void tearDown(void)
{
}

static void *test_bitstring_atomic_set_multi_threaded_worker(void *v_arg)
{
	struct test_bitstring_atomic_set_multi_threaded_arg *arg;
	size_t i;
	fiber_semaphore *s, *e;

	arg = (struct test_bitstring_atomic_set_multi_threaded_arg *)v_arg;
	s = arg->start_sem;
	e = arg->end_sem;

	for (i = 0; i < NUM_SET_ITERATIONS; ++i) {
		size_t j;

		TEST_ASSERT_FALSE(fiber_sem_wait_fn_ptr(s));
		for (j = 0; j < BS_BIT_NUM; ++j) {
			fiber_bitstring_set_atomic(&bitstring, j, 1,
						   FIBER_ATOMIC_ACQ_REL,
						   FIBER_ATOMIC_ACQUIRE);
		}
		TEST_ASSERT_FALSE(fiber_sem_post_fn_ptr(e));
	}
	pthread_exit(NULL);
	return NULL;
}

void test_bitstring_atomic_set_multi_threaded(void)
{
	struct test_bitstring_atomic_set_multi_threaded_arg
		args[NUM_SET_THREADS];
	tid tids[NUM_SET_THREADS];
	fiber_semaphore start_sem;
	fiber_semaphore end_sem;
	size_t i;

	TEST_ASSERT_FALSE(fiber_sem_init_fn_ptr(&start_sem, 0));
	TEST_ASSERT_FALSE(fiber_sem_init_fn_ptr(&end_sem, 0));

	for (i = 0; i < NUM_SET_THREADS; ++i) {
		args[i].start_sem = &start_sem;
		args[i].end_sem = &end_sem;
		TEST_ASSERT_FALSE(fiber_thread_create_fn_ptr(
			&tids[i],
			test_bitstring_atomic_set_multi_threaded_worker,
			&args[i]));
	}

	for (i = 0; i < NUM_SET_ITERATIONS; ++i) {
		size_t j;

		for (j = 0; j < NUM_SET_THREADS; ++j) {
			TEST_ASSERT_FALSE(fiber_sem_post_fn_ptr(&start_sem));
		}
		for (j = 0; j < NUM_SET_THREADS; ++j) {
			TEST_ASSERT_FALSE(fiber_sem_wait_fn_ptr(&end_sem));
		}

		for (j = 0; j < BS_BIT_NUM; ++j) {
			fiber_bitstring_word value;
			value = fiber_bitstring_get_atomic(
				&bitstring, j, FIBER_ATOMIC_ACQUIRE);
			TEST_ASSERT(value);
		}
	}

	for (i = 0; i < NUM_SET_THREADS; ++i) {
		void *res;
		TEST_ASSERT_FALSE(fiber_thread_join_fn_ptr(&tids[i], &res));
		(void)res;
	}

	TEST_ASSERT_FALSE(fiber_sem_destroy_fn_ptr(&start_sem));
	TEST_ASSERT_FALSE(fiber_sem_destroy_fn_ptr(&end_sem));
}

static void test_bitstring_atomic_get_runner(fiber_bitstring_word init_bit,
					     fiber_bitstring_word unset_bit)
{
	fiber_bitstring_word init_pattern;
	fiber_bitstring_word value;
	size_t i;

	init_pattern = ((fiber_bitstring_word)1 << init_bit);
	fiber_bitstring_init(&bitstring, init_pattern);

	for (i = 0; i < bitstring.word_num; ++i) {
		value = fiber_bitstring_get_atomic(
			&bitstring,
			(FIBER_BITSTRING_BITS_PER_WORD * i) + init_bit,
			FIBER_ATOMIC_RELAXED);
		TEST_ASSERT(value);
		value = fiber_bitstring_get_atomic(
			&bitstring,
			(FIBER_BITSTRING_BITS_PER_WORD * i) + unset_bit,
			FIBER_ATOMIC_RELAXED);
		TEST_ASSERT_FALSE(value);
	}
}

void test_bitstring_atomic_get_single_threaded(void)
{
	test_bitstring_atomic_get_runner(0, FIBER_BITSTRING_BITS_PER_WORD - 1);
	test_bitstring_atomic_get_runner(FIBER_BITSTRING_BITS_PER_WORD - 1, 0);
}

static void test_bitstring_atomic_set_runner(fiber_bitstring_word set_bit)

{
	fiber_bitstring_word value;

	fiber_bitstring_init(&bitstring, 0);

	value = fiber_bitstring_get_atomic(&bitstring, set_bit,
					   FIBER_ATOMIC_RELAXED);
	TEST_ASSERT_FALSE(value);

	fiber_bitstring_set(&bitstring, set_bit, 1);
	value = fiber_bitstring_get_atomic(&bitstring, set_bit,
					   FIBER_ATOMIC_RELAXED);
	TEST_ASSERT(value);

	fiber_bitstring_set(&bitstring, set_bit, 0);
	value = fiber_bitstring_get_atomic(&bitstring, set_bit,
					   FIBER_ATOMIC_RELAXED);
	TEST_ASSERT_FALSE(value);
}

void test_bitstring_atomic_set_single_threaded(void)
{
	test_bitstring_atomic_set_runner(0);
	test_bitstring_atomic_set_runner(BS_BIT_NUM / 2);
	test_bitstring_atomic_set_runner(BS_BIT_NUM - 1);
}

int main(void)
{
	UNITY_BEGIN();

	RUN_TEST(test_bitstring_atomic_set_multi_threaded);
	RUN_TEST(test_bitstring_atomic_get_single_threaded);
	RUN_TEST(test_bitstring_atomic_set_single_threaded);
	RUN_TEST(test_bitstring_atomic_set_multi_threaded);

	return UNITY_END();
}
