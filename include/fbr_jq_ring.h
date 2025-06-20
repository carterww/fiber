// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Carter Williams

/**
 * @file fbr_jq_ring.h
 * @brief Lock-free ring buffer job queue implementation.
 *
 * This file provides the declarations of the ring buffer job
 * queue's functions. To use this queue, @ref fbr_init_options::queue_ops
 * should be set to @ref FBR_JQ_RING_QUEUE_OPS.
 *
 * These functions implement a fixed sized lock-free FIFO job queue.
 * It uses [`ck_ring`](https://github.com/concurrencykit/ck/blob/master/include/ck_ring.h)
 * and a lock free bitmap allocator.
 */

#ifndef FBR_JQ_RING_H
#define FBR_JQ_RING_H

#include <stdint.h>

#include <fbr.h>

/**
 * @brief Initializes a ring buffer job queue of length `cap`.
 *
 * @param cap          Ring buffer's capacity. Must be a power of 2 and
 *                     greater than or equal to 4.
 * @param buffer       Buffer job queue should place its structures.
 * @param buffer_size  Size of the buffer in bytes.
 * @param allocator    malloc and free functions. Not used by this job
 *                     queue.
 *
 * @returns FBR_EOK and a valid pointer to the queue on success. An
 *          error otherwise.
 *          - `FBR_EINVAL` if `cap` was not a power of 2 or greater than
 *            or equal to 4.
 *          - `FBR_EINVLD_SIZE` if the buffer is not large enough. This
 *            should not happen and if it does there is a bug.
 *          - `FBR_EINVAL` if the buffer is not properly aligned to
 *            `FBR_ALIGNMENT_MIN`. This is indicative of a bug.
 */
struct fbr_queue_init_result fbr_jq_ring_init(uint32_t cap, void *buffer,
					      size_t buffer_size,
					      struct fbr_allocator allocator);
/**
 * @brief Does nothing.
 *
 * The ring buffer job queue does not use any dynamic memory allocation
 * and only uses the buffer it is given. All of its contents are freed
 * when the pool's single buffer is freed.
 */
void fbr_jq_ring_free(void *vqueue);

/**
 * @brief Pushes a job onto the queue if there is room.
 *
 * The ring buffer job queue does not grow or shrink with demand so
 * it is possible for this to fail. There are two options to consider
 * on failure:
 * 1. Increase the size of the job queue. This cannot be done in place,
 *    but if failures are common you may want to consider using a larger
 *    queue for future pools of similar loads.
 * 2. Use a backoff strategy to try again.
 *
 * @param vqueue  Pointer to the queue.
 * @param job     Job to push onto the queue. The queue copies the
 *                contents of the job into its structures.
 *
 * @returns The number of jobs actually pushed onto the queue. `0`
 *          if the queue is full, `1` otherwise.
 */
uint32_t fbr_jq_ring_push(void *vqueue, const struct fbr_job *job);

/**
 * @brief Pops a job from the queue if it is not empty.
 *
 * An interesting note is that the pop function must set the job
 * id in `job_entry` before the job is no longer visible in the
 * queue. In the ring buffer job queue's case, the job id of
 * `job_entry` is set after dequeuing the job from `ck_ring`
 * but before the entry is actually freed from the bitmap
 * allocator. @ref fbr_jq_ring_job_in_queue then iterates over all
 * the allocated entries instead of the `ck_ring` entries directly
 * to check if a job is in the queue.
 *
 * @param vqueue     Pointer to the queue.
 * @param job_out    Buffer whose contents will be set to the job
 *                   to the popped job's.
 * @param job_entry  The popping thread's job entry that contains
 *                   information about the job it is currently
 *                   executing.
 *
 * @returns The number of jobs actually popped from the queue. `0`
 *          if there are no jobs, `1` otherwise.
 */
uint32_t fbr_jq_ring_pop(void *vqueue, struct fbr_job *job_out,
			 struct fbr_job_entry *job_entry);

/**
 * @brief Checks if the job queue has a job with id `job_id`.
 *
 * @param vqueue  Pointer to the queue.
 * @param job_id  ID of the job to search for.
 *
 * @returns `true` if the job was found, `false` otherwise.
 */
bool fbr_jq_ring_job_in_queue(void *vqueue, uint64_t job_id);

/**
 * @brief Calculates the size in bytes needed by the queue.
 *
 * The pool is allocated from a big contiguous buffer, and the
 * pool or user must know how much memory the queue needs before
 * allocating the buffer.
 *
 * @param cap  Capacity of the queue.
 *
 * @returns The minimum size in bytes the ring buffer job queue
 *          would require. @ref fbr_jq_ring_init expects a buffer of
 *          at least this size.
 */
size_t fbr_jq_ring_size_required(uint32_t cap);

/// Helper macro to define the appropriate `fbr_queue_ops` for
/// `fbr_init_options`
#define FBR_JQ_RING_QUEUE_OPS                                        \
	{                                                            \
		fbr_jq_ring_push,	  fbr_jq_ring_pop,           \
		fbr_jq_ring_init,	  fbr_jq_ring_free,          \
		fbr_jq_ring_job_in_queue, fbr_jq_ring_size_required, \
	}

#endif /* FBR_JQ_RING_H */
