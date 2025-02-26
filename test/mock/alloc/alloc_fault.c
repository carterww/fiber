#include "fiber.h"

#include "src/threading.h"
#include "test/mock/alloc/alloc_trace.h"
#include "test/unity.h"

#define LOCK() TEST_ASSERT_EQUAL(0, fiber_mutex_lock(&alloc_trace_mutex))
#define UNLOCK() TEST_ASSERT_EQUAL(0, fiber_mutex_unlock(&alloc_trace_mutex))

extern fiber_mutex alloc_trace_mutex;
static unsigned long normal_malloc_count = 0;

void alloc_fault_init(void)
{
	alloc_trace_init();
}

void alloc_fault_destroy(void)
{
	alloc_trace_destroy();
}

void alloc_fault_verify(void)
{
	alloc_trace_verify();
}

void alloc_fault_reset(malloc_function_t _malloc, free_function_t _free,
		       unsigned long n)
{
	alloc_trace_reset(_malloc, _free);
	normal_malloc_count = n;
}

void *alloc_fault_malloc(size_t size)
{
	LOCK();
	if (normal_malloc_count == 0) {
		UNLOCK();
		return NULL;
	}
	--normal_malloc_count;
	UNLOCK();
	return alloc_trace_malloc(size);
}

void alloc_fault_free(void *ptr)
{
	alloc_trace_free(ptr);
}
