/* See LICENSE file for copyright and license details. */

#ifndef _FBR_DEBUG_H
#define _FBR_DEBUG_H

#include <stdio.h>
#include <stdlib.h>

#define fbr_static_assert(expr, message) \
	typedef char fbr_static_assert_##message[(expr) ? 1 : -1]

#define fbr_aligned(ptr, align) \
	((((uintptr_t)ptr) & (uintptr_t)(align - 1)) == 0)

#if FIBER_BUILD_OPT_COMPILE_ASSERTS != 0

/* Assert macro used to ensure an assumption is true. This assert statement
 * is used when expr must be true. If it is false, the program cannot continue
 * because it can lead to improper behavior.
 * If the assertion fails, a debug statement is printed to stderr and the
 * program exits.
 */
#define fbr_assert(expr)                                                  \
	do {                                                              \
		if (!(expr)) {                                            \
			fprintf(stderr, "[%s:%d] ASSERTION FAILED: %s\n", \
				__FILE__, __LINE__, #expr);               \
			exit(1);                                          \
		}                                                         \
	} while (0)

/* A panic macro that should be used when the program has entered an unrecoverable
 * state. It prints a debug statement to stderr and exits the program.
 */
#define fbr_panic(exit_code)                                               \
	do {                                                               \
		fprintf(stderr, "[%s:%d] PANIC: %d\n", __FILE__, __LINE__, \
			exit_code);                                        \
		exit(1);                                                   \
	} while (0)

#else
#define fbr_assert(expr) \
	do {             \
	} while (0)
#define fbr_panic(exit_code) \
	do {                 \
	} while (0)
#endif /* FIBER_COMPILE_ASSERTS != 0 */

#endif /* _FBR_DEBUG_H */
