#include <stdint.h>

#include "test/unity.h"

#include "fiber.h"

#define CLAMP_BOOL(bool) ((bool) ? 1 : 0)

void setUp(void)
{
}

void tearDown(void)
{
}

void test_capability_enum_out_of_range(void)
{
	enum fiber_capability_option lo;
	enum fiber_capability_option hi;

	lo = (enum fiber_capability_option)(-1);
	hi = (enum fiber_capability_option)FIBER_CAPABILITY_ENUM_END;
	TEST_ASSERT_FALSE(fiber_capability_get(lo));
	TEST_ASSERT_FALSE(fiber_capability_get(hi));
}

/* If this passes it isn't a 100% guarantee that correct values are returned but
 * it's good enough. Testing this for every config would be a nighmare so we'll
 * just do the current one.
 */
void test_capability_current_build(void)
{
	int asserts;
	int jid_overflow;
	int fifo_queue;
	int env_norm;
	int env_debug;
	int env_test;
	int lib_pthread;
	int atomic_gcc;
	int atomic_clang;

	asserts = fiber_capability_get(FIBER_CAPABILITY_ASSERTS);
	jid_overflow =
		fiber_capability_get(FIBER_CAPABILITY_CHECK_JID_OVERFLOW);
	fifo_queue = fiber_capability_get(FIBER_CAPABILITY_FIBER_FIFO_QUEUE);
	env_norm = fiber_capability_get(FIBER_CAPABILITY_BUILD_ENV_NORM);
	env_debug = fiber_capability_get(FIBER_CAPABILITY_BUILD_ENV_DEBUG);
	env_test = fiber_capability_get(FIBER_CAPABILITY_BUILD_ENV_TEST);
	lib_pthread =
		fiber_capability_get(FIBER_CAPABILITY_THREADING_LIB_PTHREAD);
	atomic_gcc = fiber_capability_get(
		FIBER_CAPABILITY_ATOMIC_OPERATIONS_IMPL_GCC);
	atomic_clang = fiber_capability_get(
		FIBER_CAPABILITY_ATOMIC_OPERATIONS_IMPL_CLANG);

	TEST_ASSERT_FALSE(CLAMP_BOOL(asserts) ^ CLAMP_BOOL(FIBER_COMPILE_ASSERTS));
	TEST_ASSERT_FALSE(CLAMP_BOOL(jid_overflow) ^ CLAMP_BOOL(FIBER_COMPILE_CHECK_JID_OVERFLOW));
	TEST_ASSERT_FALSE(CLAMP_BOOL(fifo_queue) ^ CLAMP_BOOL(FIBER_COMPILE_FIBER_FIFO_QUEUE));
	/* If there's a better way to do this I'd love to know */
#if defined(FIBER_BUILD_ENV_NORM)
	TEST_ASSERT_TRUE(env_norm);
#else
	TEST_ASSERT_FALSE(env_norm);
#endif
#if defined(FIBER_BUILD_ENV_DEBUG)
	TEST_ASSERT_TRUE(env_debug);
#else
	TEST_ASSERT_FALSE(env_debug);
#endif
#if defined(FIBER_BUILD_ENV_TEST)
	TEST_ASSERT_TRUE(env_test);
#else
	TEST_ASSERT_FALSE(env_test);
#endif
#if defined(FIBER_THREADING_LIB_PTHREAD)
	TEST_ASSERT_TRUE(lib_pthread);
#else
	TEST_ASSERT_FALSE(lib_pthread);
#endif
#if defined(FIBER_ATOMIC_OPERATIONS_IMPL_GCC)
	TEST_ASSERT_TRUE(atomic_gcc);
#else
	TEST_ASSERT_FALSE(atomic_gcc);
#endif
#if defined(FIBER_ATOMIC_OPERATIONS_IMPL_CLANG)
	TEST_ASSERT_TRUE(atomic_clang);
#else
	TEST_ASSERT_FALSE(atomic_clang);
#endif
	/* Make sure the enum values are mutually exclusive */
	TEST_ASSERT_EQUAL(1, CLAMP_BOOL(env_norm) + CLAMP_BOOL(env_debug) +
				     CLAMP_BOOL(env_test));
	TEST_ASSERT_EQUAL(1, CLAMP_BOOL(lib_pthread));
	TEST_ASSERT_EQUAL(1, CLAMP_BOOL(atomic_gcc) + CLAMP_BOOL(atomic_clang));
}

int main(void)
{
	UNITY_BEGIN();

	RUN_TEST(test_capability_enum_out_of_range);
	RUN_TEST(test_capability_current_build);

	return UNITY_END();
}

#undef CLAMP_BOOL
