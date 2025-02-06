/* See LICENSE file for copyright and license details. */

#ifndef FIBER_H
#define FIBER_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

/** --- VERSION --- **/
#define FIBER_VERSION_MAJOR (0)
#define FIBER_VERSION_MINOR (4)
#define FIBER_VERSION_PATCH (0)

struct fiber_version {
	int major;
	int minor;
	int patch;
};

/** --- CONFIG --- **/

/** Type Definitions **/

/* These must be signed */
typedef int tpsize; /* Threads in pool */
typedef int qsize; /* Queue size */
typedef long jid; /* Fiber job ID */
#define FIBER_TPSIZE_MAX (INT_MAX)
#define FIBER_QSIZE_MAX (INT_MAX)
#define FIBER_JID_MAX (LONG_MAX)
#define FIBER_JID_MIN (LONG_MIN)

/** Debugging Options **/

/* If 0, runtime assertions will not be compiled. */
#define FIBER_ASSERTS (1)

/** Core Options **/

/* Whether to use pthreads as the underlying threads library. Currently, this
 * is the only supported option. If this is 0, you must declare the necessary
 * macros and types in src/threading.h and implement the functions declared in
 * src/threading.h. See src/threading.h and src/threading_pthread.c on how
 * this can be done.
 * If you do implement src/threading.h's interface with a different threads
 * library, I'll gladly merge it.
 */
#define FIBER_USE_PTHREADS (1)

/* If 0, fiber will not check if job ids overflow. This can be problematic if
 * the type jid is < 64 bits because a negative job id is invalid.
 */
#define FIBER_CHECK_JID_OVERFLOW (1)
/* Define overflow check if the max JID is < 64 bits. This is a safety thing.
 * Override at your own risk...
 */
#if FIBER_JID_MAX < INT64_MAX && FIBER_CHECK_JID_OVERFLOW == 0
#undef FIBER_CHECK_JID_OVERFLOW
#define FIBER_CHECK_JID_OVERFLOW 1
#endif

/** --- END CONFIG --- **/

/* Opaque fiber_pool struct. The definition is in src/fiber_internal.h */
struct fiber_pool;

struct fiber_job {
	jid job_id;
	void *(*job_func)(void *arg);
	void *job_arg;
};

struct fiber_queue_init_result {
	int error;
	void *queue;
};

/* Queue Vtable.
 * For more information on what these functions do/how they behave, see the README in
 * src/queue.
 */
struct fiber_queue_operations {
	/* These four functions are required */
	int (*push)(void *queue, struct fiber_job *job, uint32_t flags);
	int (*pop)(void *queue, struct fiber_job *buffer, uint32_t flags);
	struct fiber_queue_init_result (*init)(qsize capacity,
					       void *(*malloc)(size_t),
					       void (*free)(void *));
	void (*free)(void *queue);

	/* Optional */
	qsize (*length)(void *queue);
};

struct fiber_pool_init_options {
	struct fiber_queue_operations *queue_ops;
	void *(*malloc)(size_t size);
	void (*free)(void *ptr);
	tpsize threads_number;
	qsize queue_length;
};

/* Result of fiber_init. pool is a valid pointer iff error = 0. */
struct fiber_init_result {
	int error;
	struct fiber_pool *pool;
};

/* Responsible for initializing all resources needed for the thread pool and
 * starting each thread. After fiber_init returns successfully, threads will
 * be awaiting work. Do not initialize a pool that has already been initialized.
 * @param opts -> A struct of params and options to initialize the new pool.
 *   queue_ops:     A struct which holds the queue implementation functions.
 *                  Fiber copies the data from the struct.
 *  malloc:         The allocator you would like Fiber to use.
 *  free:           The free functions corresponding to malloc.
 *  threads_number: The number of threads to create and start. Must be > 0.
 *  queue_length:   The length of the queue. This parameter will be passed
 *                  to the queue init function provided in queue_ops. Must be
 *                  > 0.
 * @returns: A struct that contains a possible error code and a pointer to a fiber_pool struct.
 * If error = 0, pool is a valid pointer. Otherwise the operation failed and pool was not
 * allocated.
 * @error FBR_ENOMEM -> malloc returned a NULL pointer or a resource could not be initialized
 * due to insufficient memory.
 * @error FBR_ENULL_ARGS -> opts is NULL.
 * @error FBR_EINVLD_SIZE -> threads_number or queue_length are not > 0.
 * @error FBR_EQUEOPS_NONE ->  queue_ops is NULL or one of the four required functions
 * is NULL.
 * @error FBR_ENO_ALLOC -> malloc or free is NULL.
 * @error FBR_ENO_RSC -> A new thread or mutex could not be initialized due to insufficient
 * system resources.
 * @error FBR_EPTHRD_PERM -> A new thread or mutex could not be initialized due to
 * insufficient permissions.
 * @error FBR_ESEM_RNG -> A semaphore could not be initialized because the value
 * was too large. This 
 * @error queue_ops.init -> An error from the queue initialization function. Check the
 * queue's header file to see which errors it returns.
 */
struct fiber_init_result fiber_init(struct fiber_pool_init_options *opts);

/* Pushes a job onto the job queue.
 * @param pool -> The thread pool to add work to.
 * @param job -> The job to push. A job_id will be assigned by Fiber.
 * @param queue_flags -> Flags to pass to the queue push function. Every
 * queue implementation should implement FIBER_BLOCK and FIBER_NO_BLOCK.
 * A custom implementation may have other flags.
 * @returns: 0 on success, an error otherwise.
 * @error FBR_ENULL_ARGS -> pool, job, or job_func are NULL.
 * @error FBR_EPUSH_JOB -> The queue implementation's push function
 * returned an error.
 */
jid fiber_job_push(struct fiber_pool *pool, struct fiber_job *job,
		   uint32_t queue_flags);

/* Frees the resources allocated by the pool. If you care about the work
 * being done by the threads in the pool, fiber_wait should be called to
 * ensure all jobs have finished. Calling this function may cancel a thread
 * while it is doing a job.
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
 * A thread can be freed in one of two conditions:
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
 * @error FBR_EPOOL_UNINIT -> pool was not properly initialized.
 */
int fiber_threads_remove(struct fiber_pool *pool, tpsize threads_num);

/* Add more threads to the pool.
 * @param pool -> The pool to add threads to.
 * @param threads_num -> The number of threads to add.
 * @returns -> 0 on succes, an error otherwise.
 * @error FBR_ENULL_ARGS -> pool is NULL.
 * @error FBR_EINVLD_SIZE -> threads_num is not > 0.
 * @error FBR_ENOMEM -> Could not allocate the new threads due to insufficient
 * memory.
 * @error FBR_ETHRD_LIMIT -> Could not create the new threads because the max
 * number of threads was reached.
 */
int fiber_threads_add(struct fiber_pool *pool, tpsize threads_num);

/* Get the current number of threads in the pool.
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

/* Get the version information of the library. This is useful if you
 * are compiling against an object file and the header may be
 * newer.
 * @returns -> A struct containing the major, minor, and patch version
 * numbers.
 */
struct fiber_version fiber_libversion(void);

/* Ensures the version of Fiber is compatible with the header file's version.
 * @returns -> True if they are compatible, false if they are not.
 */
static int fiber_libversion_compatible(void)
{
	struct fiber_version libversion;
	libversion = fiber_libversion();
	/* A major version of 0 is a special case. Anything goes and the header file should
	 * always be in sync with the library.
         */
	if (libversion.major == 0 || FIBER_VERSION_MAJOR == 0) {
		return libversion.major == FIBER_VERSION_MAJOR &&
		       libversion.minor == FIBER_VERSION_MINOR &&
		       libversion.patch == FIBER_VERSION_PATCH;
	}
	/* If the header file is has a newer major version, it may contain more
	 * features.
         */
	if (libversion.major < FIBER_VERSION_MAJOR) {
		return 0;
	}
	if (libversion.major == FIBER_VERSION_MAJOR &&
	    libversion.minor < FIBER_VERSION_MINOR) {
		return 0;
	}
	/* At this point we know the major version is not 0 and either
	 * 1. The library's major version is greater than the header file's.
	 * 2. The library's major version is equal to the header file's and
	 *    the library's minor version is greater than or equal to the header
	 *    file's.
         */
	return 1;
}

/** ERROR CODES **/

#define FBR_EPUSH_JOB (-1)
#define FBR_EINVLD_JOB FBR_EPUSH_JOB
#define FBR_EMTX_INIT (-2)
#define FBR_ENULL_ARGS (-3)
#define FBR_EINVLD_SIZE (-4)
#define FBR_EQUE_NULL (-5)
#define FBR_ENO_RSC (-6)
#define FBR_EPTHRD_PERM (-7)
#define FBR_ESEM_RNG (-8)
#define FBR_EQUEOPS_NONE (-9)
#define FBR_EPOOL_UNINIT (-10)
#define FBR_ETHRD_LIMIT (-11)
#define FBR_ENO_ALLOC (-12)
#define FBR_ENOMEM (-13)

/** Flags **/

/* Job Queue Flags */
#define FIBER_QUEUE_BLOCK (1 << 31)
#define FIBER_QUEUE_NO_BLOCK 0

#endif /* FIBER_H */
