/* See LICENSE file for copyright and license details. */

#include "fiber/fiber.h"

/** Version of lib **/
static const struct fiber_version libversion = {
	FIBER_VERSION_MAJOR,
	FIBER_VERSION_MINOR,
	FIBER_VERSION_PATCH,
};

struct fiber_version fiber_libversion(void)
{
	return libversion;
}

#if defined(FIBER_BUILD_ENV_TEST)
#include "test_internal.h"
struct fiber_test_internal_version fiber_test_internal_version = { &libversion };
#endif /* FIBER_BUILD_ENV_TEST */
