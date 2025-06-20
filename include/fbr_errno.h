// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Carter Williams

#ifndef FBR_ERRNO_H
#define FBR_ERRNO_H

#include <limits.h>

/**
 * @brief Error enum used throughout Fiber.
 */
enum fbr_errno {
	FBR_EOK = 0,
	FBR_EGENERIC,
	FBR_EINVAL,
	FBR_ENOMEM,
	FBR_EAGAIN,
	FBR_ETIMEDOUT,
	FBR_ENO_RSC,
	FBR_ENULL_ARG,
	FBR_EINVLD_SIZE,
	FBR_ETHRD_LIMIT,
	FBR_ENO_ALLOC,
	FBR_EQUEUE_PUSH,
	FBR_ENOTSUP,
};
typedef enum fbr_errno fbr_errno_t;

#endif /* FBR_ERRNO_H */
