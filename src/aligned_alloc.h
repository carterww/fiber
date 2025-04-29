/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_ALIGNED_ALLOC_H
#define _FIBER_ALIGNED_ALLOC_H

#include <stddef.h>

#include "fiber/fiber.h"

/* Dynamically allocates memory aligned to alignment by using an underlying general
 * purpose malloc function. There are two important restrictions with this function:
 * 1. Memory allocated through this function must be freed using aligned_free.
 * 2. Alignment must be >= sizeof(void *) and a power of 2.
 * @param malloc -> General purpose malloc function to use as the underlying allocator.
 * @param size -> Number of bytes to allocate.
 * @param alignment -> Memory alignment.
 * @returns -> NULL if malloc returns NULL, a valid pointer aligned to alignment otherwise.
 */
void *aligned_malloc(malloc_function_t malloc, size_t size, size_t alignment);

/* Frees memory allocated from aligned_malloc. There are two important restrictions with this
 * function:
 * 1. The free function must correspond to the malloc function used to allocate ptr.
 * 2. ptr must be the exact pointer returned from aligned_malloc. If it is not, you will be
 *    pulling your hair out over weird UB bugs.
 * @param free -> Free function that corresponds to the malloc function passed to aligned_malloc.
 * @param ptr -> Pointer to aligned memory allocated with aligned_malloc. You should assert the
 * alignment before passing the pointer. You have been warned.
 */
void aligned_free(free_function_t free, void *ptr);

#endif /* _FIBER_ALIGNED_ALLOC_H */
