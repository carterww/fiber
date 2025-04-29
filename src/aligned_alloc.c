#include <stddef.h>

#include "aligned_alloc.h"
#include "debug.h"
#include "fiber/fiber.h"

static_assert(sizeof(unsigned long) >= sizeof(void *),
	      ulong_used_in_ptr_arithmetic);

void *aligned_malloc(malloc_function_t malloc, size_t size, size_t alignment)
{
	void *maybe_aligned;
	void *aligned;
	size_t alignment_lower;
	size_t maybe_aligned_size;
	unsigned long maybe_aligned_start, maybe_aligned_end;
	unsigned long aligned_start, aligned_end;

	alignment_lower = alignment - 1;
	maybe_aligned_size = size + sizeof(void *) + alignment_lower;

	fiber_assert(size != 0);
	/* NOTE: This ensures storing the original pointer in the header will be
	 * aligned correctly.
	 */
	fiber_assert(alignment >= sizeof(void *));
	fiber_assert((alignment & alignment_lower) == 0);

	maybe_aligned = malloc(maybe_aligned_size);
	if (maybe_aligned == NULL) {
		return NULL;
	}
	maybe_aligned_start = (unsigned long)maybe_aligned;
	maybe_aligned_end = maybe_aligned_start + maybe_aligned_size;

	aligned = (void *)((maybe_aligned_start + sizeof(void *) +
			    alignment_lower) &
			   ~alignment_lower);
	aligned_start = (unsigned long)aligned;
	aligned_end = aligned_start + size;

	/* These are just sanity checks */
	fiber_assert(((size_t)aligned & alignment_lower) == 0);
	fiber_assert(maybe_aligned_start <= aligned_start - sizeof(void *));
	fiber_assert(maybe_aligned_end >= aligned_end);

	*(void **)(aligned_start - sizeof(void *)) = maybe_aligned;

	return aligned;
}

void aligned_free(free_function_t free, void *ptr)
{
	void *base_malloc_ptr;
	unsigned long aligned_start;

	fiber_assert(free != NULL);
	fiber_assert(ptr != NULL);

	aligned_start = (unsigned long)ptr;
	base_malloc_ptr = *(void **)(aligned_start - sizeof(void *));
	free(base_malloc_ptr);
}
