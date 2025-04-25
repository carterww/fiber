#ifndef _FIBER_TEST_MOCK_ALLOC_TRACE_H
#define _FIBER_TEST_MOCK_ALLOC_TRACE_H

#include <stdlib.h>

#include "fiber/fiber.h"

void alloc_trace_init(void);
void alloc_trace_destroy(void);

void alloc_trace_verify(void);
void alloc_trace_reset(malloc_function_t _malloc, free_function_t _free);

void *alloc_trace_malloc(size_t size);
void alloc_trace_free(void *ptr);

#endif /* _FIBER_TEST_MOCK_ALLOC_TRACE_H */
