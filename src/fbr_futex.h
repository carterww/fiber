// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Carter Williams

#ifndef _FBR_FUTEX_H
#define _FBR_FUTEX_H

#include <stdbool.h>
#include <stdint.h>

#include <fbr_errno.h>

fbr_errno_t fbr_futex_wait(uint32_t *futex, uint32_t expected);

fbr_errno_t fbr_futex_wait_timeout(uint32_t *futex, uint32_t expected,
				   unsigned long *timeout_ms);

fbr_errno_t fbr_futex_wake(uint32_t *futex, uint32_t *num_threads);

#endif /* _FBR_FUTEX_H */
