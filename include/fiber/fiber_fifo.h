/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_QUEUE_FIFO_H
#define _FIBER_QUEUE_FIFO_H

#include "fiber/fiber.h"

/* Opaque job queue struct */
struct fiber_fifo_jq;

/* Function definitions for the job queue's VTable */

/* Intializes the job queue by allocating a static array of fiber_jobs.
 * @param capacity -> The maximum jobs that can be in the queue.
 * @param malloc -> Function to allocate memory.
 * @param free -> Function to free memory allocated with the malloc param.
 * @returns A struct that contains an error code and a pointer to the queue.
 * If error = 0, the queue pointer is valid. Otherwise the operation failed
 * and the queue could not be initialized.
 * @error FBR_ENOMEM -> A resource could not be initialized or allocated due to
 * insufficient memory.
 * @error FBR_ESEM_RNG -> The semaphore could not be initialized because its intial
 * value is too large. This likely indiciates that capacity is too large.
 */
struct fiber_queue_init_result fiber_queue_fifo_init(qsize capacity,
						     malloc_function_t _malloc,
						     free_function_t _free);

/* Pushes a job onto the queue if there is room.
 * @param queue -> Pointer to the job queue struct.
 * @param job -> The job to push onto the queue. The contents of the job are copied.
 * @param flags -> Flags that alter the behavior of the push function. The following
 * are valid flags:
 *   - FIBER_QUEUE_BLOCK: Indiciates the function should block until there is room
 *     on the queue.
 * @returns 0 on success, an error otherwise.
 * @error FBR_EAGAIN -> There is no room on the queue and FIBER_QUEUE_BLOCK
 * was not set.
 */
int fiber_queue_fifo_push(void *queue, const struct fiber_job *job,
			  unsigned long flags);

/* Pops a job from the queue and puts its contents inside buffer.
 * @param queue -> Pointer to the job queue struct.
 * @param buffer -> Pointer to a fiber_job struct that can be filled with the job's
 * contents.
 * @param flags -> Flags that alter the behavior of the pop function. The following
 * are valid flags:
 *   - FIBER_QUEUE_BLOCK: Indiciates the function should block until a job is
 *     available to pop.
 * @returns 0 on success, an error otherwise.
 * @error FBR_EAGAIN -> There is no job to pop from the queue and FIBER_QUEUE_BLOCK
 * was not set.
 */
int fiber_queue_fifo_pop(void *queue, struct fiber_job *buffer,
			 unsigned long flags);

/* Frees the resources allocated by the queue.
 * @param queue -> Pointer to the job queue struct.
 */
void fiber_queue_fifo_free(void *queue);

/* Returns the number of jobs in the queue.
 * @param queue -> Pointer to the job queue struct.
 * @returns The number of jobs in the queue.
 */
qsize fiber_queue_fifo_length(void *queue);

/* rvalue of a fiber_queue_operations that can be used to easily set
 * the VTable to the proper functions.
 */
#define FIBER_FIFO_QUEUE_OPERATIONS                                   \
	{                                                             \
		fiber_queue_fifo_push, fiber_queue_fifo_pop,          \
			fiber_queue_fifo_init, fiber_queue_fifo_free, \
			fiber_queue_fifo_length,                      \
	}

#endif /* _FIBER_QUEUE_FIFO_H */
