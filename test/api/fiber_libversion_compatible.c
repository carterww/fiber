#include "test/unity.h"

static int h_major = -1;
static int h_minor = -1;
static int h_patch = -1;

#define FIBER_VERSION_MAJOR (h_major)
#define FIBER_VERSION_MINOR (h_minor)
#define FIBER_VERSION_PATCH (h_patch)

#include "fiber.h"

/* This points to the internal fiber_version exposed by test_internal.h.
 * Each test should set this beforehand to their desired values.
 */
static struct fiber_version libversion = { -1, -1, -1 };

static void set_libversion(int mj, int mn, int p)
{
	libversion.major = mj;
	libversion.minor = mn;
	libversion.patch = p;
}

static void set_header_version(int mj, int mn, int p)
{
	h_major = mj;
	h_minor = mn;
	h_patch = p;
}

struct fiber_version fiber_libversion(void)
{
	TEST_ASSERT(libversion.major > -1);
	TEST_ASSERT(libversion.minor > -1);
	TEST_ASSERT(libversion.patch > -1);
	TEST_ASSERT(h_major > -1);
	TEST_ASSERT(h_minor > -1);
	TEST_ASSERT(h_patch > -1);
	return libversion;
}

void setUp(void)
{
	set_libversion(-1, -1, -1);
	set_header_version(-1, -1, -1);
}

void tearDown(void)
{
}

void test_libversion_compatible_major_zero(void)
{
	set_libversion(0, 1, 1);
	set_header_version(0, 1, 1);
	TEST_ASSERT_TRUE(fiber_libversion_compatible());

	set_libversion(0, 0, 1);
	set_header_version(0, 1, 1);
	TEST_ASSERT_FALSE(fiber_libversion_compatible());
}

void test_libversion_compatible_major_different_nonzero(void)
{
	set_libversion(2, 0, 0);
	set_header_version(1, 0, 0);

	TEST_ASSERT_FALSE(fiber_libversion_compatible());
}

void test_libversion_compatible_minor_header_newer(void)
{
	set_libversion(1, 0, 0);
	set_header_version(1, 1, 0);

	TEST_ASSERT_FALSE(fiber_libversion_compatible());
}

void test_libversion_compatible_minor_lib_equal_or_newer(void)
{
	set_libversion(1, 1, 0);
	set_header_version(1, 0, 0);
	TEST_ASSERT_TRUE(fiber_libversion_compatible());

	set_libversion(1, 0, 0);
	set_header_version(1, 0, 0);
	TEST_ASSERT_TRUE(fiber_libversion_compatible());
}

void test_libversion_compatible_patch_different(void)
{
	set_libversion(1, 0, 25);
	set_header_version(1, 0, 0);

	TEST_ASSERT_TRUE(fiber_libversion_compatible());
}

int main(void)
{
	UNITY_BEGIN();

	RUN_TEST(test_libversion_compatible_major_zero);
	RUN_TEST(test_libversion_compatible_major_different_nonzero);
	RUN_TEST(test_libversion_compatible_minor_header_newer);
	RUN_TEST(test_libversion_compatible_minor_lib_equal_or_newer);
	RUN_TEST(test_libversion_compatible_patch_different);

	return UNITY_END();
}
