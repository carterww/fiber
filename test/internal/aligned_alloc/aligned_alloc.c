#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "src/aligned_alloc.h"

#include "test/unity.h"

#define ARR_LEN(arr) sizeof(arr) / sizeof(*(arr))

/* Aligned alloc doesn't support alignment lower than a pointer */
static const size_t almin = sizeof(void *);
#define alnext(i) (almin << i)

static const size_t sizes[] = { 1,  2,	 4,    8,    16,    32,
				64, 128, 1024, 4096, 10000, 100000 };
static const size_t alignments[] = { alnext(0), alnext(1), alnext(2), alnext(3),
				     alnext(4), alnext(5), alnext(6) };

void setUp(void)
{
}

void tearDown(void)
{
}

static void verify_aligned_ptr(void *ptr, size_t size, size_t alignment)
{
	unsigned long uptr;

	uptr = (unsigned long)ptr;
	/* Make sure pointer is actually aligned */
	TEST_ASSERT((uptr & (alignment - 1)) == 0);
	/* Try to write to see if segfault occurs */
	memset(ptr, 1, size);
}

void test_aligned_alloc_combinations(void)
{
	size_t i, j;
	void *aligned_ptr;
	for (i = 0; i < ARR_LEN(sizes); ++i) {
		for (j = 0; j < ARR_LEN(alignments); ++j) {
			aligned_ptr =
				aligned_malloc(malloc, sizes[i], alignments[j]);
			TEST_ASSERT_NOT_NULL(aligned_ptr);
			verify_aligned_ptr(aligned_ptr, sizes[i],
					   alignments[j]);
			aligned_free(free, aligned_ptr);
		}
	}
}

int main(void)
{
	UNITY_BEGIN();

	RUN_TEST(test_aligned_alloc_combinations);

	return UNITY_END();
}
