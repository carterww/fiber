#include "fiber.h"

#include "src/threading.h"

#include "src/test_internal.h"
#include "test/mock/alloc/alloc_fault.h"
#include "test/mock/alloc/alloc_trace.h"
#include "test/unity.h"

extern fiber_mutex alloc_trace_mutex;
/* We ALWAYS use the Vtable exported by the threading module in internal testing
 * modules. If we don't and are using threading_trace_fault, threading_trace_fault
 * wil atttempt to track out mutex calls here and ruin everything.
 */
extern const struct fiber_threading_vtable threading_vtable;
#define LOCK() \
	TEST_ASSERT_EQUAL(0, (threading_vtable.mutex_lock(&alloc_trace_mutex)))
#define UNLOCK()             \
	TEST_ASSERT_EQUAL(0, \
			  (threading_vtable.mutex_unlock(&alloc_trace_mutex)))

static unsigned long fail_after = 0;

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
	fail_after = n;
}

void *alloc_fault_malloc(size_t size)
{
	LOCK();
	if (fail_after == 0) {
		UNLOCK();
		return NULL;
	}
	--fail_after;
	UNLOCK();
	return alloc_trace_malloc(size);
}

void alloc_fault_free(void *ptr)
{
	alloc_trace_free(ptr);
}
