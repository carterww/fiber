#include <stdlib.h>

#include "fiber/fiber.h"
#include "fiber_lock/mutex.h"

#include "alloc_trace.h"
#include "test/unity.h"

/* We ALWAYS use the Vtable exported by the threading module in internal testing
 * modules. If we don't and are using threading_trace_fault, threading_trace_fault
 * wil atttempt to track out mutex calls here and ruin everything.
 */
extern const struct fiber_threading_vtable threading_vtable;
#define LOCK() \
	TEST_ASSERT_EQUAL(0, (fiber_mutex_lock_fn_ptr(&alloc_trace_mutex)))
#define UNLOCK() \
	TEST_ASSERT_EQUAL(0, (fiber_mutex_unlock_fn_ptr(&alloc_trace_mutex)))

#define PTRS_LENGTH() (sizeof(ptrs) / sizeof(*ptrs))
#define ALLOC_TRACE_MAX_PTRS (256)

union max_align_t {
	char c;
	short s;
	int i;
	long l;
	float f;
	double d;
	long double ld;
	void *p;
};

/* I want to add N bytes of padding before and after a chunk of memory allocated
 * from malloc. I used 4 bytes and returned ptr + 4 to the user but this obviously
 * messes with the alignment of 8 byte pointers (thank you undefined sanitizer for
 * letting me find that). 
 *
 * This approach attempts to keep the largest required alignment returned by malloc
 * by setting the number of padding bytes to the alignment of the largest primitive.
 * This probably doesn't work in all cases, but it is ok for most.
 *
 * TODO(Carter): This uses a non-standard compiler extension to get the alignment. I
 * need to put all non-standard test suite tools behind interfaces.
 */
#define MALLOC_PADDING_BYTES (__alignof__(union max_align_t))
#define PADDING_BYTE_VALUE (0x5a)

struct alloc_trace_ptr {
	void *ptr;
	size_t user_size;
	size_t padding;
};

/* alloc_fault uses this as well */
fiber_mutex alloc_trace_mutex;
static struct alloc_trace_ptr ptrs[ALLOC_TRACE_MAX_PTRS] = { 0 };
static unsigned long malloc_calls = 0;
static unsigned long free_calls = 0;

static malloc_function_t _malloc = NULL;
static free_function_t _free = NULL;

#define canary_loop_setup(val)                 \
	char *user_ptr;                        \
	char *start_front;                     \
	char *start_end;                       \
	unsigned long i;                       \
                                               \
	user_ptr = (char *)val->ptr;           \
	start_front = user_ptr - val->padding; \
	start_end = user_ptr + val->user_size; \
                                               \
	for (i = 0; i < val->padding; ++i)

static void set_canary(struct alloc_trace_ptr *val)
{
	canary_loop_setup(val)
	{
		start_front[i] = PADDING_BYTE_VALUE;
		start_end[i] = PADDING_BYTE_VALUE;
	}
}

static void check_canary(struct alloc_trace_ptr *val)
{
	canary_loop_setup(val)
	{
		TEST_ASSERT_EQUAL(PADDING_BYTE_VALUE, start_front[i]);
		TEST_ASSERT_EQUAL(PADDING_BYTE_VALUE, start_end[i]);
	}
}

static void insert_ptr(char *user_ptr, size_t user_size, size_t padding)
{
	unsigned long i;

	for (i = 0; i < PTRS_LENGTH(); ++i) {
		if (ptrs[i].ptr != NULL) {
			continue;
		}
		ptrs[i].ptr = (void *)user_ptr;
		ptrs[i].user_size = user_size;
		ptrs[i].padding = padding;
		set_canary(&ptrs[i]);
		return;
	}
	UNLOCK();
	TEST_FAIL_MESSAGE("alloc_trace has a full list of ptrs. Consider "
			  "raising ALLOC_TRACE_MAX_PTRS or making ptr tracking "
			  "dyanmic.");
}

static void *remove_ptr(char *user_ptr)
{
	unsigned long i;
	char *res = NULL;

	for (i = 0; i < PTRS_LENGTH(); ++i) {
		if (ptrs[i].ptr != (void *)user_ptr) {
			continue;
		}
		check_canary(&ptrs[i]);
		res = (char *)ptrs[i].ptr - ptrs[i].padding;
		ptrs[i].ptr = NULL;
		ptrs[i].user_size = 0;
		ptrs[i].padding = 0;
		return res;
	}
	UNLOCK();
	TEST_FAIL_MESSAGE(
		"alloc_trace encountered a ptr that was never returned by malloc.");
}

void alloc_trace_init(void)
{
	int res;
	res = fiber_mutex_init_fn_ptr(&alloc_trace_mutex);
	TEST_ASSERT_EQUAL(0, res);
}

void alloc_trace_destroy(void)
{
	int res;
	res = fiber_mutex_destroy_fn_ptr(&alloc_trace_mutex);
	TEST_ASSERT_EQUAL(0, res);
}

void alloc_trace_verify(void)
{
	unsigned long i;
	unsigned long count = 0;

	for (i = 0; i < PTRS_LENGTH(); ++i) {
		if (ptrs[i].ptr != NULL) {
			++count;
		}
	}
	TEST_ASSERT_EQUAL_MESSAGE(
		malloc_calls, free_calls,
		"expected = malloc calls, actual = free calls");
	TEST_ASSERT_EQUAL_MESSAGE(0, count,
				  "Found a memory leak in alloc_trace_verify");
}

void alloc_trace_reset(malloc_function_t _mal, free_function_t _fr)
{
	unsigned long i;

	for (i = 0; i < PTRS_LENGTH(); ++i) {
		ptrs[i].ptr = NULL;
		ptrs[i].user_size = 0;
		ptrs[i].padding = 0;
	}
	malloc_calls = 0;
	free_calls = 0;
	_malloc = _mal;
	_free = _fr;
}

void *alloc_trace_malloc(size_t size)
{
	void *ptr;
	char *user_ptr;

	TEST_ASSERT_NOT_NULL_MESSAGE(
		_malloc, "alloc_trace never received a malloc function.");
	/* Allocate MALLOC_PADDING bytes on boths ends of the user's
         * new memory. We will set known values here and make sure those
         * values are still there on free.
         */
	ptr = _malloc(size + MALLOC_PADDING_BYTES * 2);
	user_ptr = (char *)ptr + MALLOC_PADDING_BYTES;
	if (ptr != NULL) {
		LOCK();
		++malloc_calls;
		insert_ptr((void *)user_ptr, size, MALLOC_PADDING_BYTES);
		UNLOCK();
	}
	return user_ptr;
}

void alloc_trace_free(void *ptr)
{
	char *user_ptr;

	TEST_ASSERT_NOT_NULL_MESSAGE(
		_free, "alloc_trace never received a free function.");
	TEST_ASSERT_NOT_NULL_MESSAGE(ptr, "alloc_trace_free given NULL ptr");

	LOCK();
	++free_calls;
	user_ptr = remove_ptr(ptr);
	UNLOCK();
	_free((void *)user_ptr);
}
