/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_H
#define _FIBER_H

#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>

#include "job_queue.h"

/* List of definitions to change compilation
 * 1. FIBER_ASSERTS: If defined, compile assert statements. These
 *    will exit the program if the assertion is not true.
 * 2. FIBER_CHECK_JID_OVERFLOW: If defined (or jid's max is less than
 *    int64's max), Fiber will not allow job ids to overflow. A negative
 *    job id represents an invalid job, so your program will experience
 *    failure if the job_id incrementer overflows.
 * 3. FIBER_NO_DEFAULT_QUEUE: If defined, Fiber will not compile the
 *    default queue implementation. If this is defined, Fiber assumes
 *    you will provide your own queue implementation at runtime through
 *    fiber_pool_init_options.
 */

typedef int tpsize; // Type to represent number of threads in pool
#define THREAD_POOL_SIZE_MAX INT_MAX

/** Thread Management **/

struct fiber_thread {
	struct fiber_thread *next;
	pthread_t thread_id;
	jid job_id;
};

/** Pool **/

struct fiber_pool {
	pthread_mutex_t lock;
	jid job_id_prev;
	const struct fiber_queue_operations *queue_ops;
	void *job_queue;
	struct fiber_thread *thread_head;
	tpsize threads_number;
	tpsize threads_working;
	sem_t threads_sync;
	tpsize threads_kill_number;
	uint32_t pool_flags;
	void *(*malloc)(size_t __size);
	void (*free)(void *__ptr);
};

struct fiber_pool_init_options {
	struct fiber_queue_operations *queue_ops;
	void *(*malloc)(size_t __size);
	void (*free)(void *__ptr);
	tpsize threads_number;
	qsize queue_length;
};

/* Responsible for initializing all resources needed for the thread pool and
 * starting each thread. After fiber_init returns successfully, threads will
 * be awaiting work. Do not initialize a pool that has already been initialized.
 * @param pool -> The pool to initialize.
 * @param opts -> A struct of init params.
 *   queue_ops:     A struct which holds the queue implementation functions.
 *                  If this is NULL and FIBER_NO_DEFAULT_QUEUE is not a defined
 *                  macro, then a fixed queue size FIFO implementation is used.
 *                  Fiber copies the data from the struct.
 *  malloc:         The allocator you would like Fiber to use. If NULL, libc's
 *                  malloc is used.
 *  free:           The free functions corresponding to malloc. If NULL, libc's
 *                  malloc is used.
 *  threads_number: The number of threads to create and start. Must be > 0.
 *  queue_length:   The length of the queue. This parameter will be passed
 *                  to the queue init function provided in queue_ops. Must be
 *                  > 0.
 * @returns: 0 on success, an error code otherwise.
 * @error FBR_ENULL_ARGS -> pool or opts are NULL.
 * @error FBR_EINVLD_SIZE -> threads_number or queue_length are not > 0
 * @error FBR_EQUEOPS_NONE -> FIBER_NO_DEFAULT_QUEUE is defined and queue_ops
 *                             is NULL or the required queue_ops provided are
 *                             not all provided.
 * @error FBR_ENO_RSC -> pthread or pthread_mutex could not be initialized due
 *                        to insufficient system resources (other than memory).
 * @error FBR_EPTHRD_PERM -> pthread or pthread_mutex could not be initialized
 *                            due to insufficient permissions.
 * @error FBR_ESEM_RNG -> A semaphore could not be initialized because the value
 *                         was too large. The provided queue size was likely too
 *                         large.
 * @error FBR_EQUE_NULL -> The queue pointer was null after calling initialize
 *                          on the queue.
 * @error ENOMEM -> malloc returned a NULL pointer.
 */
int fiber_init(struct fiber_pool *pool, struct fiber_pool_init_options *opts);

/* Pushes a job onto the job queue.
 * @param pool -> The thread pool to queue work.
 * @param job -> The job to push. A job_id will be assigned by Fiber.
 * @param queue_flags -> Flags to pass to the queue push function. Every
 * queue implementation should implement FIBER_BLOCK and FIBER_NO_BLOCK.
 * A custom implementation may take other options.
 * @returns: 0 on success, an error otherwise.
 * @error FBR_ENULL_ARGS -> pool, job, or job_func were NULL.
 * @error FBR_EPUSH_JOB -> An invalid job_id. The queue push function
 * returned a positive integer. This indicates an error, but we could
 * not return it as is because it would look like a valid job id.
 * @error -int -> The queue implementation returned some negative
 * error code.
 *   - In the default fiber_queue_fifo_push, this number is most likely
 *     EAGAIN * -1. This indicates FIBER_NO_BLOCK was used and the queue
 *     was full.
 */
jid fiber_job_push(struct fiber_pool *pool, struct fiber_job *job,
		   uint32_t queue_flags);

/* Frees the resources allocated by the pool. Before calling this,
 * please call fiber_threads_working to ensure no threads are working.
 * The free may fail if a thread holds an internal lock. If this occurs,
 * the threads will be cancelled but there will likely be a resource leak.
 * @param pool -> The thread pool to free.
 */
void fiber_free(struct fiber_pool *pool);

/* Blocks until the job queue is empty. Once the job queue is empty
 * (all threads asleep) this function will return.
 * @param pool -> The pool to wait on.
 */
void fiber_wait(struct fiber_pool *pool);

/* Get the number of jobs currently waiting to be executed in the job queue.
 * @param pool -> The pool which contains the job queue to check.
 * @returns -> The number of jobs waiting in the queue.
 * @error FBR_ENULL_ARGS -> pool, pool->job_queue, or pool->queue_ops is NULL.
 * @error FBR_EQUEOPS_NONE -> there is no "length" operation defined for the
 * queue.
 */
qsize fiber_jobs_pending(struct fiber_pool *pool);

/* Remove threads_num threads from the pool. Threads that are currently
 * executing jobs will not be cancelled. Fiber does not know when these
 * threads will be free; only that they will be freed as soon as possible.
 * A thread can be free in one of two conditions:
 * 1. It is currently sleeping, waiting for work.
 * 2. The thread just finished executing a job and is about to pop another
 *    job from the queue.
 * @param pool -> The pool from which to remove threads.
 * @param threads_num -> The number of threads to remove. If this number is
 * greater than the current number of threads, all threads in the pool will
 * be cancelled as well as any newly created threads until the quota is met.
 * @returns -> 0 on success, an error otherwise.
 * @error FBR_ENULL_ARGS -> pool is NULL.
 * @error FBR_EINVLD_SIZE -> threads_num is less than 1.
 * @error FBR_EPOOL_UNINIT -> pool was not properly initialized. It is missing
 * queue_ops or queue_ops->push.
 * @error FBR_EPUSH_JOB -> There was an error attempting to push the wake job
 * onto the queue. If this error occurs the queue implementation refused to
 * push the job.
 * @error -int -> fiber_push_job received a negative error code from the
 * underlying queue push implementation. Hopefully these don't conflic
 * with the error codes above :)
 */
int fiber_threads_remove(struct fiber_pool *pool, tpsize threads_num);

/* Add more threads to the pool.
 * @param pool -> The pool to add threads to.
 * @param threads_num -> The number of threads to add.
 * @returns -> 0 on succes, an error otherwise.
 * @error FBR_ENULL_ARGS -> pool is NULL.
 * @error EAGAIN -> pthread_mutex_lock failed.
 */
int fiber_threads_add(struct fiber_pool *pool, tpsize threads_num);

/* Get the current number of threads currently running in the pool.
 * @param pool -> The pool to check.
 * @returns -> The number of threads the pool has allocated and working or
 * a negative number representing an error.
 * @error FBR_ENULL_ARGS -> pool is NULL.
 */
tpsize fiber_threads_number(struct fiber_pool *pool);

/* Get the current number of threads currently running a user job.
 * @param pool -> The pool to check.
 * @returns -> The number of working threads or a negative number
 * representing an error.
 * @error FBR_ENULL_ARGS -> pool is NULL.
 */
tpsize fiber_threads_working(struct fiber_pool *pool);

#define FIBER_POOL_FLAG_WAIT (1 << 0)
#define FIBER_POOL_FLAG_KILL_N (1 << 1)

/* ERROR CODES */

#define FBR_EPUSH_JOB -1
#define FBR_EINVLD_JOB FBR_EPUSH_JOB
#define FBR_EMTX_INIT -2
#define FBR_ENULL_ARGS -3
#define FBR_EINVLD_SIZE -4
#define FBR_EQUE_NULL -5
#define FBR_ENO_RSC -6
#define FBR_EPTHRD_PERM -7
#define FBR_ESEM_RNG -8
#define FBR_EQUEOPS_NONE -9
#define FBR_EPOOL_UNINIT -10

#endif // _FIBER_H
