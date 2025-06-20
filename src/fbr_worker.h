// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Carter Williams

#ifndef _FBR_WORKER_H
#define _FBR_WORKER_H

#include "fbr_internal.h"

void *fbr_worker_runner_internal(void *pool_ptr);

fbr_errno_t fbr_worker_runner_external(struct fbr_pool *pool,
				       unsigned long thread_id);

void fbr_worker_runner_loop(struct fbr_pool *pool);

#endif /* _FBR_WORKER_H */
