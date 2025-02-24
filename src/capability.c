/* See LICENSE file for copyright and license details. */

#include "fiber.h"
#include "utils.h"

/* This file is very messy and macro heavy... I'm sorry */

#define CAPABILITY_BIT(bool, shift) \
        (((bool) ? 1 : 0) << (shift % 8))

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

/* This bitstring will hold information about the features compiled into
 * the binary. This will allow the user to query the library at runtime
 * and determine if it can perform a certain action.
 * Currently, all options are either a boolean or enum. The enum options
 * will be split into multiple boolean options. All the options will be
 * stored in a bitstring.
 */
static unsigned char capability_bitstring[] = {
        /* 0-7 fiber_capability_option values */
        (
                CAPABILITY_BIT(FIBER_COMPILE_ASSERTS, FIBER_CAPABILITY_ASSERTS) |
                CAPABILITY_BIT(FIBER_COMPILE_FIBER_FIFO_QUEUE, FIBER_CAPABILITY_FIBER_FIFO_QUEUE) |
                CAPABILITY_BIT(FIBER_BUILD_ENV_NORM_EXISTS, FIBER_CAPABILITY_BUILD_ENV_NORM) |
                CAPABILITY_BIT(FIBER_BUILD_ENV_DEBUG_EXISTS, FIBER_CAPABILITY_BUILD_ENV_DEBUG) |
                CAPABILITY_BIT(FIBER_BUILD_ENV_TEST_EXISTS, FIBER_CAPABILITY_BUILD_ENV_TEST) |
                CAPABILITY_BIT(FIBER_THREADING_LIB_PTHREAD_EXISTS, FIBER_CAPABILITY_THREADING_LIB_PTHREAD) |
                CAPABILITY_BIT(FIBER_ATOMIC_OPERATIONS_IMPL_GCC_EXISTS, FIBER_CAPABILITY_ATOMIC_OPERATIONS_IMPL_GCC) |
                CAPABILITY_BIT(FIBER_ATOMIC_OPERATIONS_IMPL_CLANG_EXISTS, FIBER_CAPABILITY_ATOMIC_OPERATIONS_IMPL_CLANG) |
                0
        )
};

int fiber_capability_get(enum fiber_capability_option opt)
{
        unsigned int idx;
        unsigned int shift;

        if (opt < 0 || opt >= FIBER_CAPABILITY_ENUM_END) {
                return 0;
        }

        idx = ((unsigned int)opt) / 8;
        shift = ((unsigned int)opt) % 8;

        fiber_assert(idx < sizeof(capability_bitstring));

        return capability_bitstring[idx] & (1 << shift);
}

#undef FIBER_BUILD_ENV_NORM_EXISTS
#undef FIBER_BUILD_ENV_DEBUG_EXISTS
#undef FIBER_BUILD_ENV_TEST_EXISTS
#undef FIBER_THREADING_LIB_PTHREAD_EXISTS
#undef FIBER_ATOMIC_OPERATIONS_IMPL_GCC_EXISTS
#undef FIBER_ATOMIC_OPERATIONS_IMPL_CLANG_EXISTS
#undef CAPABILITY_BIT

#if defined(FIBER_BUILD_ENV_TEST)
#include "test_internal.h"
struct fiber_test_internal_capability fiber_test_internal_capability = {
        capability_bitstring,
        sizeof(capability_bitstring)
};
#endif /* FIBER_BUILD_ENV_TEST */
