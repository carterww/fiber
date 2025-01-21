/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_UTILS_H
#define _FIBER_UTILS_H

#include <stdio.h>
#include <stdlib.h>

#include "fiber.h"

#if defined(__GNUC__)
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x) (x)
#define unlikely(x) (x)
#endif /* If builtin expect defined */

#if FIBER_ASSERTS != 0

/* Assert macro used to ensure an assumption is true. This assert statement
 * is used when expr must be true. If it is false, the program cannot continue
 * because it can lead to improper behavior.
 * If the assertion fails, a debug statement is printed to stderr and the
 * program exits.
 */
#define fiber_assert(expr)                                                     \
	if (unlikely(!(expr))) {                                               \
		fprintf(stderr, "ERR: assertion failed at %s:%d.\n", __FILE__, \
			__LINE__);                                             \
		exit(1);                                                       \
	}
#else
#define fiber_assert(expr)
#endif

/* A panic macro that should be used when the program has entered an unrecoverable
 * state. It prints a debug statement to stderr and exits the program.
 */
#define panic(exit_code)                                                  \
	do {                                                              \
		fprintf(stderr, "PANIC at %s:%d.\n", __FILE__, __LINE__); \
		exit(1);                                                  \
	} while (0)

#endif /* _FIBER_UTILS_H */
