/* See LICENSE file for copyright and license details. */

#include "bitstring.h"
#include "debug.h"
#include "fiber/fiber.h"

/* This file is very messy and macro heavy... I'm sorry */

/* These macros are just defined, not 0 or 1. Make a new macro that is 0 or 1 */

#if defined(FIBER_BUILD_ENV_NORM)
#define FIBER_BUILD_ENV_NORM_EXISTS (1)
#else
#define FIBER_BUILD_ENV_NORM_EXISTS (0)
#endif /* FIBER_BUILD_ENV_NORM */

#if defined(FIBER_BUILD_ENV_DEBUG)
#define FIBER_BUILD_ENV_DEBUG_EXISTS (1)
#else
#define FIBER_BUILD_ENV_DEBUG_EXISTS (0)
#endif /* FIBER_BUILD_ENV_DEBUG */

#if defined(FIBER_BUILD_ENV_TEST)
#define FIBER_BUILD_ENV_TEST_EXISTS (1)
#else
#define FIBER_BUILD_ENV_TEST_EXISTS (0)
#endif /* FIBER_BUILD_ENV_TEST */

#if defined(FIBER_THREADING_LIB_PTHREAD)
#define FIBER_THREADING_LIB_PTHREAD_EXISTS (1)
#else
#define FIBER_THREADING_LIB_PTHREAD_EXISTS (0)
#endif /* FIBER_THREADING_LIB_PTHREAD */

#if defined(FIBER_ATOMIC_OPERATIONS_IMPL_GCC)
#define FIBER_ATOMIC_OPERATIONS_IMPL_GCC_EXISTS (1)
#else
#define FIBER_ATOMIC_OPERATIONS_IMPL_GCC_EXISTS (0)
#endif /* FIBER_ATOMIC_OPERATIONS_IMPL_GCC */

#if defined(FIBER_ATOMIC_OPERATIONS_IMPL_CLANG)
#define FIBER_ATOMIC_OPERATIONS_IMPL_CLANG_EXISTS (1)
#else
#define FIBER_ATOMIC_OPERATIONS_IMPL_CLANG_EXISTS (0)
#endif /* FIBER_ATOMIC_OPERATIONS_IMPL_CLANG */

static_assert(sizeof(fiber_bitstring_word) * FIBER_PLATFORM_BITS_PER_BYTE >= 32,
	      fiber_capability_expects_gte_32_bits_bitstring_words);

/* This bitstring will hold information about the features compiled into
 * the binary. This will allow the user to query the library at runtime
 * and determine if it can perform a certain action.
 * Currently, all options are either a boolean or enum. The enum options
 * will be split into multiple boolean options. All the options will be
 * stored in a bitstring.
 */
static fiber_bitstring_word capability_bitstring[] = {
	/* 0-31 fiber_capability_option values */
	(0 |
	 FIBER_BITSTRING_BIT(FIBER_COMPILE_ASSERTS, FIBER_CAPABILITY_ASSERTS) |
	 FIBER_BITSTRING_BIT(FIBER_COMPILE_FIBER_FIFO_QUEUE,
			     FIBER_CAPABILITY_FIBER_FIFO_QUEUE) |
	 FIBER_BITSTRING_BIT(FIBER_BUILD_ENV_NORM_EXISTS,
			     FIBER_CAPABILITY_BUILD_ENV_NORM) |
	 FIBER_BITSTRING_BIT(FIBER_BUILD_ENV_DEBUG_EXISTS,
			     FIBER_CAPABILITY_BUILD_ENV_DEBUG) |
	 FIBER_BITSTRING_BIT(FIBER_BUILD_ENV_TEST_EXISTS,
			     FIBER_CAPABILITY_BUILD_ENV_TEST) |
	 FIBER_BITSTRING_BIT(FIBER_THREADING_LIB_PTHREAD_EXISTS,
			     FIBER_CAPABILITY_THREADING_LIB_PTHREAD) |
	 FIBER_BITSTRING_BIT(FIBER_ATOMIC_OPERATIONS_IMPL_GCC_EXISTS,
			     FIBER_CAPABILITY_ATOMIC_OPERATIONS_IMPL_GCC) |
	 FIBER_BITSTRING_BIT(FIBER_ATOMIC_OPERATIONS_IMPL_CLANG_EXISTS,
			     FIBER_CAPABILITY_ATOMIC_OPERATIONS_IMPL_CLANG))
};

static struct fiber_bitstring capability_bitstring_struct = {
	sizeof(capability_bitstring) / sizeof(*capability_bitstring),
	capability_bitstring,
};

int fiber_capability_get(enum fiber_capability_option opt)
{
	if (opt >= FIBER_CAPABILITY_ENUM_END) {
		return 0;
	}

	return fiber_bitstring_get(&capability_bitstring_struct, (size_t)opt) ?
		       1 :
		       0;
}

#undef FIBER_BUILD_ENV_NORM_EXISTS
#undef FIBER_BUILD_ENV_DEBUG_EXISTS
#undef FIBER_BUILD_ENV_TEST_EXISTS
#undef FIBER_THREADING_LIB_PTHREAD_EXISTS
#undef FIBER_ATOMIC_OPERATIONS_IMPL_GCC_EXISTS
#undef FIBER_ATOMIC_OPERATIONS_IMPL_CLANG_EXISTS
#undef FIBER_BITSTRING_BIT

#if defined(FIBER_BUILD_ENV_TEST)
#include "test_internal.h"
struct fiber_test_internal_capability fiber_test_internal_capability = {
	&capability_bitstring_struct,
};
#endif /* FIBER_BUILD_ENV_TEST */
