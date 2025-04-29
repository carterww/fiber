#include "src/bitstring.h"

#include "test/unity.h"

#define BS_WORD_NUM (4)
#define BS_BIT_NUM (BS_WORD_NUM * FIBER_BITSTRING_BITS_PER_WORD)

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

void test_bitstring_correct_word_num(void)
{
	size_t bits;

	for (bits = 0; bits <= bits_word_num_test_max; ++bits) {
		size_t required_words;

		required_words = bits / word_size_bits;
		if (bits % word_size_bits != 0) {
			++required_words;
		}
		TEST_ASSERT(required_words * word_size_bits >= bits);
		TEST_ASSERT(FIBER_BITSTRING_WORD_NUM(bits) * word_size_bits >=
			    bits);
	}
}

static void test_bitstring_init_runner(fiber_bitstring_word init_pattern)
{
	size_t i;

	fiber_bitstring_init(&bitstring, init_pattern);

	for (i = 0; i < bitstring.word_num; ++i) {
		TEST_ASSERT_EQUAL(init_pattern, bitstring.words[i]);
	}
}

void test_bitstring_init(void)
{
	test_bitstring_init_runner(0x5a5a);
	test_bitstring_init_runner(0);
}

static void test_bitstring_get_runner(fiber_bitstring_word init_bit,
				      fiber_bitstring_word unset_bit)
{
	fiber_bitstring_word init_pattern;
	fiber_bitstring_word value;
	size_t i;

	init_pattern = ((fiber_bitstring_word)1 << init_bit);
	fiber_bitstring_init(&bitstring, init_pattern);

	for (i = 0; i < bitstring.word_num; ++i) {
		value = fiber_bitstring_get(&bitstring, (FIBER_BITSTRING_BITS_PER_WORD * i) + init_bit);
		TEST_ASSERT(value);
		value = fiber_bitstring_get(&bitstring, (FIBER_BITSTRING_BITS_PER_WORD * i) + unset_bit);
		TEST_ASSERT_FALSE(value);
	}
}

void test_bitstring_get(void)
{
	test_bitstring_get_runner(0, FIBER_BITSTRING_BITS_PER_WORD - 1);
	test_bitstring_get_runner(FIBER_BITSTRING_BITS_PER_WORD - 1, 0);
}

static void test_bitstring_set_runner(fiber_bitstring_word set_bit)
				      
{
	fiber_bitstring_word value;

	fiber_bitstring_init(&bitstring, 0);

	value = fiber_bitstring_get(&bitstring, set_bit);
	TEST_ASSERT_FALSE(value);

	fiber_bitstring_set(&bitstring, set_bit, 1);
	value = fiber_bitstring_get(&bitstring, set_bit);
	TEST_ASSERT(value);

	fiber_bitstring_set(&bitstring, set_bit, 0);
	value = fiber_bitstring_get(&bitstring, set_bit);
	TEST_ASSERT_FALSE(value);
}

void test_bitstring_set(void)
{
	test_bitstring_set_runner(0);
	test_bitstring_set_runner(BS_BIT_NUM / 2);
	test_bitstring_set_runner(BS_BIT_NUM - 1);
}

int main(void)
{
	UNITY_BEGIN();

	RUN_TEST(test_bitstring_correct_word_num);
	RUN_TEST(test_bitstring_init);
	RUN_TEST(test_bitstring_get);
	RUN_TEST(test_bitstring_set);

	return UNITY_END();
}
