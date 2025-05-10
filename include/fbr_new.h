/* See LICENSE file for copyright and license details. */

#ifndef FBR_H
#define FBR_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include <fbr_errno.h>

/* The pool will be an opaque struct. I'm making no guarantees about
 * the internal of this struct.
 */
struct fbr_pool;
typedef struct fbr_pool fbr_pool_t;

struct fbr_allocator {
	void *(*malloc)(size_t);
	void (*free)(void *);
};
typedef struct fbr_allocator fbr_allocator_t;

struct fbr_job {
	uint64_t id;
	void *(*cb)(void *);
	void *cb_arg;
};
typedef struct fbr_job fbr_job_t;

struct fbr_queue_init_result {
	enum fbr_errno error;
	void *queue;
};

struct fbr_queue_ops {
	unsigned int (*push)(void *, const struct fbr_job *, uint32_t);
	unsigned int (*pop)(void *, struct fbr_job *, uint32_t);
	struct fbr_queue_init_result (*init)(unsigned int,
					     struct fbr_allocator);
	void (*free)(void *);
};
typedef struct fbr_queue_ops fbr_queue_ops_t;

struct fbr_init_options {
	struct fbr_queue_ops queue_ops;
	struct fbr_allocator allocator;
	unsigned int thread_num;
	unsigned int queue_len;
	unsigned int thread_max;
	unsigned int callers_max;
};
typedef struct fbr_init_options fbr_init_options_t;

struct fbr_init_result {
	enum fbr_errno error;
	struct fbr_pool *pool;
};
typedef struct fbr_init_result fbr_init_result_t;

fbr_init_result_t fbr_init(const fbr_init_options_t *options);

void fiber_free(fbr_pool_t *pool);

fbr_errno_t fbr_job_push(fbr_pool_t *pool, fbr_job_t *job, uint32_t flags);

fbr_errno_t fbr_job_push_raw(fbr_pool_t *pool, const fbr_job_t *job,
			     uint32_t flags);

fbr_errno_t fbr_wait(fbr_pool_t *pool);

fbr_errno_t fbr_wait_job(fbr_pool_t *pool, uint64_t job_id);

fbr_errno_t fbr_thread_add(fbr_pool_t *pool, unsigned int thread_num);

fbr_errno_t fbr_thread_remove(fbr_pool_t *pool, unsigned int thread_num);

unsigned int fbr_thread_num(const fbr_pool_t *pool);

unsigned int fbr_thread_working(const fbr_pool_t *pool);

unsigned int fbr_jobs_pending(const fbr_pool_t *pool);

#endif /* FBR_H */
