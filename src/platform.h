/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_PLATFORM_H
#define _FIBER_PLATFORM_H

#include <stddef.h>

/* TODO: This is a stand in for something that should be done at build
 * time.
 */

#define FIBER_PLATFORM_BITS_PER_BYTE (8)
#define FIBER_PLATFORM_CACHE_LINE_BYTES (64)

#define FIBER_PLATFORM_CACHE_LINE_ALIGNED_BYTES(type)                   \
	((size_t)(sizeof(type) + FIBER_PLATFORM_CACHE_LINE_BYTES - 1) & \
	 (~((size_t)FIBER_PLATFORM_CACHE_LINE_BYTES - 1)))

#endif /* _FIBER_PLATFORM_H */
