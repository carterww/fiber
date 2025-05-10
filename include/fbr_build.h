/* See LICENSE file for copyright and license details. */

#ifndef FBR_BUILD_H
#define FBR_BUILD_H

#include <stdbool.h>

struct fbr_version {
	unsigned short major;
	unsigned short minor;
	unsigned short patch;
};

enum fbr_build_mode {
	FBR_BUILD_MODE_RELEASE = 0,
	FBR_BUILD_MODE_DEBUG,
	FBR_BUILD_MODE_TEST,
};

enum fbr_threading_model {
	FBR_THREADING_MODEL_POSIX = 0,
};

enum fbr_atomics_impl {
	FBR_ATOMICS_IMPL_CK = 0,
};

enum fbr_mutex_impl {
	FBR_MUTEX_IMPL_POSIX = 0,
};

enum fbr_semaphore_impl {
	FBR_SEMAPHORE_IMPL_POSIX = 0,
};

extern const struct fbr_version FBR_LIBVERSION;
extern const bool FBR_HAS_ASSERTS;
extern const enum fbr_build_mode FBR_BUILD_MODE;
extern const enum fbr_threading_model FBR_THREADING_MODEL;
extern const enum fbr_atomics_impl FBR_ATOMICS_IMPL;
extern const enum fbr_mutex_impl FBR_MUTEX_IMPL;
extern const enum fbr_semaphore_impl FBR_SEMAPHORE_IMPL;

#endif /* FBR_BUILD_H */
