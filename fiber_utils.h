/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_UTILS_H
#define _FIBER_UTILS_H

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#ifdef FIBER_ASSERTS
#include <stdio.h>
#include <stdlib.h>
#define assert(expr, err_msg)                                          \
	if (unlikely(!(expr))) {                                       \
		fprintf(stderr, "ERR: assert failed at %s:%d -> %s\n", \
			__FILE__, __LINE__, err_msg);                  \
		exit(1);                                               \
	}
#else
#define assert(expr, err_msg)
#endif

#endif // _FIBER_UTILS_H
