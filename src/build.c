#include <stdbool.h>

#include <fbr_build.h>

const struct fbr_version FBR_LIBVERSION = {
	FIBER_VERSION_MAJOR,
	FIBER_VERSION_MINOR,
	FIBER_VERSION_PATCH,
};

const bool FBR_HAS_ASSERTS = FIBER_BUILD_OPT_COMPILE_ASSERTS ? 1 : 0;

#if defined(FIBER_BUILD_OPT_ENV_REL)
const enum fbr_build_mode FBR_BUILD_MODE = FBR_BUILD_MODE_RELEASE;
#elif defined(FIBER_BUILD_OPT_ENV_DEBUG)
const enum fbr_build_mode FBR_BUILD_MODE = FBR_BUILD_MODE_DEBUG;
#elif defined(FIBER_BUILD_OPT_ENV_TEST)
const enum fbr_build_mode FBR_BUILD_MODE = FBR_BUILD_MODE_TEST;
#endif

const enum fbr_threading_model FBR_THREADING_MODEL = FBR_THREADING_MODEL_POSIX;
const enum fbr_atomics_impl FBR_ATOMICS_IMPL = FBR_ATOMICS_IMPL_CK;
const enum fbr_mutex_impl FBR_MUTEX_IMPL = FBR_MUTEX_IMPL_POSIX;
const enum fbr_semaphore_impl FBR_SEMAPHORE_IMPL = FBR_SEMAPHORE_IMPL_POSIX;
