#ifndef _FIBER_TEST_MOCK_ALLOC_FAULT_H
#define _FIBER_TEST_MOCK_ALLOC_FAULT_H

#include <stdlib.h>

void alloc_fault_init(void);
void alloc_fault_destroy(void);

void alloc_fault_verify(void);
void alloc_fault_reset(unsigned long normal_malloc_count);

void *alloc_fault_malloc(size_t size);
void alloc_fault_free(void *ptr);

#endif /* _FIBER_TEST_MOCK_ALLOC_FAULT_H */
