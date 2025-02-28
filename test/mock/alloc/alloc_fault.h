#ifndef _FIBER_TEST_MOCK_ALLOC_FAULT_H
#define _FIBER_TEST_MOCK_ALLOC_FAULT_H

#include <stdlib.h>

#include "fiber.h"

void alloc_fault_init(void);
void alloc_fault_destroy(void);

void alloc_fault_verify(void);
void alloc_fault_reset(malloc_function_t _malloc, free_function_t _free,
		       unsigned long fail_after);

void *alloc_fault_malloc(size_t size);
void alloc_fault_free(void *ptr);

#endif /* _FIBER_TEST_MOCK_ALLOC_FAULT_H */
