/* See LICENSE file for copyright and license details. */

#ifndef _FBR_WORKER_H
#define _FBR_WORKER_H

#include "fbr_internal.h"
#include "fbr_thread.h"

struct fbr_worker_tls {
	struct fbr_pool *pool;
	tid_t thread_id;
	unsigned int thread_idx;
	bool on_stack;
};

void *fbr_worker_runner_internal(void *pool_ptr);

void fbr_worker_runner_loop(struct fbr_pool *pool);

#endif /* _FBR_WORKER_H */
