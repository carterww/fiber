/* See LICENSE file for copyright and license details. */

#ifndef FIBER_H
#define FIBER_H

#include <limits.h>
#include <stddef.h>

/* Some test suites redefine these before including fiber.h to test
 * with different header versions. This is a simple but scuffed way
 * to avoid redefining them.
 */
#if !defined(FIBER_VERSION_MAJOR)
#define FIBER_VERSION_MAJOR (0)
#define FIBER_VERSION_MINOR (2)
#define FIBER_VERSION_PATCH (1)
#endif /* FIBER_VERSION_MAJOR */

struct fiber_version {
	int major;
	int minor;
	int patch;
};

/* These types must be signed */
typedef int tpsize; /* Threads in pool */
typedef int qsize; /* Queue size */
typedef long jid; /* Fiber job ID */

#define FIBER_TPSIZE_MAX (INT_MAX)
#define FIBER_TPSIZE_MIN (INT_MIN)
#define FIBER_QSIZE_MAX (INT_MAX)
#define FIBER_QSIZE_MIN (INT_MIN)
#define FIBER_JID_MAX (LONG_MAX)
#define FIBER_JID_MIN (LONG_MIN)

/* Function typedefs */
typedef void *(*malloc_function_t)(size_t size);
typedef void (*free_function_t)(void *ptr);

typedef void *(*fiber_job_function_t)(void *arg);

/* Opaque fiber_pool struct. The definition is in src/fiber_internal.h */
struct fiber_pool;

struct fiber_job {
	jid job_id;
	fiber_job_function_t job_func;
	void *job_arg;
};

struct fiber_queue_init_result {
	int error;
	void *queue;
};

/* Queue function typedefs */
typedef int (*fiber_queue_push_function_t)(void *queue,
					   const struct fiber_job *job,
					   unsigned long flags);
typedef int (*fiber_queue_pop_function_t)(void *queue, struct fiber_job *buffer,
					  unsigned long flags);
typedef struct fiber_queue_init_result (*fiber_queue_init_function_t)(
	qsize capacity, malloc_function_t _malloc, free_function_t _free);
typedef void (*fiber_queue_free_function_t)(void *queue);
typedef qsize (*fiber_queue_length_function_t)(void *queue);

/* Queue Vtable.
 * For more information on what these functions do/how they behave, see the README in
 * src/queue.
 */
struct fiber_queue_operations {
	fiber_queue_push_function_t push;
	fiber_queue_pop_function_t pop;
	fiber_queue_init_function_t init;
	fiber_queue_free_function_t free;
	fiber_queue_length_function_t length;
};

struct fiber_pool_init_options {
	struct fiber_queue_operations *queue_ops;
	malloc_function_t malloc;
	free_function_t free;
	tpsize threads_number;
	qsize queue_length;
};

/* Result of fiber_init. pool is a valid pointer iff error = 0. */
struct fiber_init_result {
	int error;
	struct fiber_pool *pool;
};

/* Options that can be passed to fiber_capability_get to see if support for
 * the option was compiled into the binary.
 */
enum fiber_capability_option {
	FIBER_CAPABILITY_ASSERTS = 0,
	FIBER_CAPABILITY_FIBER_FIFO_QUEUE = 1,
	FIBER_CAPABILITY_BUILD_ENV_NORM = 2,
	FIBER_CAPABILITY_BUILD_ENV_DEBUG = 3,
	FIBER_CAPABILITY_BUILD_ENV_TEST = 4,
	FIBER_CAPABILITY_THREADING_LIB_PTHREAD = 5,
	FIBER_CAPABILITY_ATOMIC_OPERATIONS_IMPL_GCC = 6,
	FIBER_CAPABILITY_ATOMIC_OPERATIONS_IMPL_CLANG = 7,

	/* This should always be the last one */
	FIBER_CAPABILITY_ENUM_END
};

/* Responsible for initializing all resources needed for the thread pool and
 * starting each thread. After fiber_init returns successfully, threads will
 * be awaiting work. Do not initialize a pool that has already been initialized.
 * @param opts -> A struct of params and options to initialize the new pool.
 *  queue_ops:      A struct which holds the queue implementation functions.
 *                  Fiber copies the data from the struct.
 *  malloc:         The allocator you would like Fiber to use.
 *  free:           The free functions corresponding to malloc.
 *  threads_number: The number of threads to create and start. Must be >= 0.
 *  queue_length:   The length of the queue. This parameter will be passed
 *                  to the queue init function provided in queue_ops. Must be
 *                  > 0.
 * @returns: A struct that contains a possible error code and a pointer to a fiber_pool struct.
 * If error = 0, pool is a valid pointer. Otherwise the operation failed and pool was not
 * allocated.
 * @error FBR_ENOMEM -> malloc returned a NULL pointer or a resource could not be initialized
 * due to insufficient memory.
 * @error FBR_ENULL_ARGS -> opts is NULL.
 * @error FBR_EINVLD_SIZE -> threads_number was < 0 or queue_length was <= 0.
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
struct fiber_init_result fiber_init(const struct fiber_pool_init_options *opts);

/* Pushes a job onto the job queue and assigns it an ID.
 * @param pool -> The thread pool to add work to.
 * @param job -> The job to push. A job_id will be assigned by Fiber.
 * @param queue_flags -> Flags to pass to the queue push function. Every
 * queue implementation should implement FIBER_BLOCK and FIBER_NO_BLOCK.
 * A custom implementation may have other flags.
 * @returns: 0 on success, an error otherwise.
 * @error FBR_ENULL_ARGS -> pool, job, or job_func are NULL.
 * @error FBR_EPUSH_JOB -> A generic error returned by the queue push function.
 * @error FBR_EAGAIN -> The queue is full and FIBER_QUEUE_BLOCK was not specified
 * in queue_flags.
 */
jid fiber_job_push(struct fiber_pool *pool, struct fiber_job *job,
		   unsigned long queue_flags);

/* Pushes a job onto the job queue but does not assign it an ID. This function
 * can be used if you already use an integer type >= 0 to identify your jobs.
 * @param pool -> Pool to push the job to.
 * @param job -> The job to push. It should have a unique number >= 0 in the
 * job_id member.
 * @param queue_flags -> Flags to pass to the job queue's push function.
 * @returns -> Your job's ID or < 0 on error.
 * @error FBR_ENULL_ARGS -> pool, job, or job_func are NULL.
 * @error FBR_EPUSH_JOB -> A generic error returned by the queue push function.
 * @error FBR_EAGAIN -> The queue is full and FIBER_QUEUE_BLOCK was not specified
 * in queue_flags.
 */
jid fiber_job_push_raw(const struct fiber_pool *pool,
		       const struct fiber_job *job, unsigned long queue_flags);

/* Frees the resources allocated by the pool. If you care about the work
 * being done by the threads in the pool, fiber_wait should be called first
 * to ensure all jobs have finished. Calling this function may cancel a thread
 * while it is doing a job.
 * @param pool -> The thread pool to free.
 */
void fiber_free(struct fiber_pool *pool);

/* Blocks until the current threads in the pool finish the jobs in the queue.
 * If there are no threads in the pool, it immediately returns.
 * @param pool -> The pool to wait on.
 * @error FBR_ENULL_ARGS -> pool was NULL.
 */
int fiber_wait(struct fiber_pool *pool);

/* Blocks until the job identified by job_id has finished.
 * @param pool -> The pool the job was queued to.
 * @param job_id -> The job ID returned from fiber_job_push or fiber_job_push_raw.
 * @error FBR_ENULL_ARGS -> pool was NULL.
 * @error FBR_EINVLD_JOB -> job_id was invalid.
 */
int fiber_wait_job(struct fiber_pool *pool, jid job_id);

/* Get the number of jobs currently waiting to be executed in the job queue.
 * @param pool -> The pool which contains the job queue to check.
 * @returns -> The number of jobs waiting in the queue.
 * @error FBR_ENULL_ARGS -> pool or pool->job_queue is NULL.
 * @error FBR_EQUEOPS_NONE -> there is no "length" operation defined for the
 * queue.
 */
qsize fiber_jobs_pending(const struct fiber_pool *pool);

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
 * I don't like this behavior, but it makes doing this much easier.
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
tpsize fiber_threads_number(const struct fiber_pool *pool);

/* Get the current number of threads currently running a user job.
 * @param pool -> The pool to check.
 * @returns -> The number of working threads or a negative number
 * representing an error.
 * @error FBR_ENULL_ARGS -> pool is NULL.
 */
tpsize fiber_threads_working(const struct fiber_pool *pool);

/* Get the version information of the library. This is useful if you
 * are compiling against an object file and the header may be
 * newer.
 * @returns -> A struct containing the major, minor, and patch version
 * numbers.
 */
struct fiber_version fiber_libversion(void);

/* Tests if the capability option opt is supported by the Fiber lib.
 * @param opt -> The option to test for.
 * @returns -> 0 if the capability is not supported, non-zero otherwise.
 */
int fiber_capability_get(enum fiber_capability_option opt);

/* Ensures the version of Fiber is compatible with the header file's version.
 * @returns -> True if they are compatible, false (0) if they are not.
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
	/* If the major versions are different there were some breaking API changes. */
	if (libversion.major != FIBER_VERSION_MAJOR) {
		return 0;
	}
	/* A newer header file may have more functions that were not built into the lib */
	if (libversion.minor < FIBER_VERSION_MINOR) {
		return 0;
	}
	/* At this point we know the following:
         * 1. The library's major version is not 0.
	 * 2. The library's major version is equal to the header file's major version.
         * 3. The library's minor version is greater than or equal to the header
         *    file's minor version.
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
#define FBR_EAGAIN (-14)
#define FBR_EINTR (-15)

/** Flags **/

/* Job Queue Flags */
#define FIBER_QUEUE_BLOCK (1UL << 31)
#define FIBER_QUEUE_NO_BLOCK (0UL)

#endif /* FIBER_H */
