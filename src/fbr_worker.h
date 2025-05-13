/* See LICENSE file for copyright and license details. */

#ifndef _FBR_WORKER_H
#define _FBR_WORKER_H

#include "fbr_internal.h"
#include "fbr_thread.h"
#include "fbr_thread_entries.h"

struct fbr_worker_tls {
	struct fbr_pool *pool;
	enum fbr_thread_type thread_type;
	union {
		tid_t thread_id_int;
		uint64_t thread_id_ext;
	} tid;
	unsigned int thread_idx;
	bool on_stack;
};

void *fbr_worker_runner_internal(void *pool_ptr);

fbr_errno_t fbr_worker_runner_external(struct fbr_pool *pool,
				       unsigned long thread_id);

void fbr_worker_runner_loop(struct fbr_pool *pool);

#endif /* _FBR_WORKER_H */
