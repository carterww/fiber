/* See LICENSE file for copyright and license details. */

#include "fiber.h"

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
