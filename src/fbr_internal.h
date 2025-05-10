/* See LICENSE file for copyright and license details. */

#ifndef _FBR_INTERNAL_H
#define _FBR_INTERNAL_H

#include "fbr_packed_counters.h"
#include <limits.h>

#include <fbr_new.h>

#include "fbr_bm_alloc.h"
#include "fbr_thread.h"

/*
struct fiber_thread {
	struct fiber_thread *next;
	tid thread_id;
};
struct fiber_pool {
	fiber_mutex lock;
	jid job_id_prev;
	struct fiber_queue_operations queue_ops;
	void *job_queue;
	struct fiber_thread *thread_head;
	tpsize threads_number;
	union fiber_twql_packed twql;
	tpsize threads_kill_number;
	unsigned long fiber_wait_epoch;
	malloc_function_t malloc;
	free_function_t free;
};
*/

enum fbr_thread_type {
	FBR_THREAD_TYPE_NONE = 0,
	FBR_THREAD_TYPE_INTERNAL = 1,
	FBR_THREAD_TYPE_EXTERNAL = 2,
};

struct fbr_thread_internal {
	tid_t id;
	int canceled;
};

struct fbr_thread_external {
	unsigned long id;
};

struct fbr_thread {
	enum fbr_thread_type type;
	union {
		struct fbr_thread_internal internal;
		struct fbr_thread_external external;
	} thread;
};

struct fbr_thread_entries {
	struct fbr_bm_alloc_meta meta;
	struct fbr_thread *array;
};

struct fbr_pool {
	void *const job_queue;
	const struct fbr_queue_ops job_queue_ops;
	uint64_t job_id_counter;

	unsigned int thread_num;
	union fbr_packed_counters_union twlo_qlhi;

	int thread_kill_num;

	struct fbr_thread_entries threads;

	const unsigned int thread_max; /* Max number of threads in pool */
	const unsigned int callers_max; /* Max number of callers */

	const struct fbr_allocator alloc;
};

/* This is just for initialization. I keep it here so I'm reminded to change both */
struct fbr_pool_mutable {
	void *job_queue;
	struct fbr_queue_ops job_queue_ops;
	uint64_t job_id_counter;

	unsigned int thread_num;
	union fbr_packed_counters_union twlo_qlhi;

	int thread_kill_num;

	struct fbr_thread_entries threads;

	unsigned int thread_max;
	unsigned int callers_max;

	struct fbr_allocator alloc;
};

#endif /* _FBR_INTERNAL_H */
