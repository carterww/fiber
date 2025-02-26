#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "test/unity.h"

#include "alloc_trace.h"

#define LOCK() TEST_ASSERT_EQUAL(0, pthread_mutex_lock(&malloc_ptr_list_mtx))
#define UNLOCK() \
	TEST_ASSERT_EQUAL(0, pthread_mutex_unlock(&malloc_ptr_list_mtx))
#define PTRS_LENGTH() (sizeof(ptrs) / sizeof(*ptrs))

#define ALLOC_TRACE_MAX_PTRS (512)

static pthread_mutex_t malloc_ptr_list_mtx = PTHREAD_MUTEX_INITIALIZER;
static void *ptrs[ALLOC_TRACE_MAX_PTRS] = { NULL };
static unsigned long malloc_calls = 0;
static unsigned long free_calls = 0;

static void insert_ptr(void *ptr)
{
	unsigned long i;

	for (i = 0; i < PTRS_LENGTH(); ++i) {
		if (ptrs[i] == NULL) {
			ptrs[i] = ptr;
			return;
		}
	}
	TEST_FAIL_MESSAGE("alloc_trace has a full list of ptrs. Consider "
			  "raising ALLOC_TRACE_MAX_PTRS or making ptr tracking "
			  "dyanmic.");
}

static void remove_ptr(void *ptr)
{
	unsigned long i;

	for (i = 0; i < PTRS_LENGTH(); ++i) {
		if (ptrs[i] == ptr) {
			ptrs[i] = NULL;
			return;
		}
	}
	TEST_FAIL_MESSAGE(
		"alloc_trace encountered a ptr that was never returned by malloc.");
}

void alloc_trace_init(void)
{
}

void alloc_trace_destroy(void)
{
	pthread_mutex_destroy(&malloc_ptr_list_mtx);
}

void alloc_trace_verify(void)
{
	unsigned long i;
	unsigned long count = 0;

	for (i = 0; i < PTRS_LENGTH(); ++i) {
		if (ptrs[i] != NULL) {
			++count;
		}
	}
	TEST_ASSERT_EQUAL_MESSAGE(
		malloc_calls, free_calls,
		"expected = malloc calls, actual = free calls");
	TEST_ASSERT_EQUAL_MESSAGE(
		0, count, "Encountered a memory leak in alloc_trace_verify");
}

void alloc_trace_reset(void)
{
	unsigned long i;

	for (i = 0; i < PTRS_LENGTH(); ++i) {
		ptrs[i] = NULL;
	}
	malloc_calls = 0;
	free_calls = 0;
}

void *alloc_trace_malloc(size_t size)
{
	void *ptr;

	ptr = malloc(size);
	LOCK();
	++malloc_calls;
	insert_ptr(ptr);
	UNLOCK();
	return ptr;
}

void alloc_trace_free(void *ptr)
{
	LOCK();
	++free_calls;
	remove_ptr(ptr);
	UNLOCK();
	free(ptr);
}
