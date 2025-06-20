/* See LICENSE file for copyright and license details. */

#ifndef FBR_H
#define FBR_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <fbr_errno.h>

struct fbr_pool;
typedef struct fbr_pool fbr_pool_t;

/**
 * @brief Allocator functions used by the pool and job queue.
 */
struct fbr_allocator {
	void *(*malloc)(size_t);
	void (*free)(void *);
};
typedef struct fbr_allocator fbr_allocator_t;

/**
 * @brief Job that a thread pool can execute.
 *
 * The job's id being unique is not a hard requirement. It only
 * matters if you execute @ref fbr_wait_job
 */
struct fbr_job {
	uint64_t id; ///< Job's unique identifier
	void *(*cb)(void *); ///< Function invoked when the job is selected
	void *cb_arg; ///< Argument passed to the job's function
};
typedef struct fbr_job fbr_job_t;

/**
 * @brief Used only by the job queue and internally.
 *
 * Each thread in the pool "posts" the job they are working on
 * for @ref fbr_wait_job. This structure is public only to
 * facilitate the job's transition from the job queue to the
 * thread it will execute on.
 */
struct fbr_job_entry {
	int active;
	uint64_t job_id;
};
typedef struct fbr_job_entry fbr_job_entry_t;

/**
 * @brief Result returned by @ref fbr_queue_ops::init
 *
 * Similar to `fbr_job_entry` this is a structure that is
 * not relevant to the user but must be public.
 */
struct fbr_queue_init_result {
	enum fbr_errno error;
	void *queue;
};

/**
 * @brief VTable of a job queue's functions.
 *
 * List of job queue implementations:
 * - @ref fbr_jq_ring.h and @ref FBR_JQ_RING_QUEUE_OPS
 */
struct fbr_queue_ops {
	uint32_t (*push)(void *, const struct fbr_job *);
	uint32_t (*pop)(void *, struct fbr_job *, struct fbr_job_entry *);
	struct fbr_queue_init_result (*init)(uint32_t, void *, size_t,
					     struct fbr_allocator);
	void (*free)(void *);
	bool (*job_in_queue)(void *, uint64_t);
	size_t (*size_required)(uint32_t);
};
typedef struct fbr_queue_ops fbr_queue_ops_t;

/**
 * @brief Options passed to @ref fbr_init.
 */
struct fbr_init_options {
	struct fbr_queue_ops queue_ops;
	/// Can have NULL members if not needed queue & buffer passed to @ref fbr_init
	struct fbr_allocator allocator;
	/// This should be 0 to use a default value.
	size_t thread_stack_size_bytes;
	/// Number of threads to initially spawn.
	uint32_t thread_num;
	/// Length of the job queue passed to @ref fbr_queue_ops::init.
	uint32_t queue_len;
	/// Maximum number of threads supported by the pool.
	uint32_t thread_max;
	/// Maximum number of threads that can call pool functions.
	uint32_t callers_max;
	/// Whether to enable support for @ref fbr_wait.
	bool wait_enable;
	/// Whether to enable support for @ref fbr_wait_job.
	bool wait_job_enable;
};
typedef struct fbr_init_options fbr_init_options_t;

/**
 * @brief Result of @ref fbr_init.
 *
 * If @ref fbr_init successfully initializes a pool, `error` will be
 * `FBR_EOK` and `pool` will be a valid pointer. If @ref fbr_init fails,
 * `pool` will be undefined and `error` will indicate the reason for
 * the failure.
 */
struct fbr_init_result {
	enum fbr_errno error;
	struct fbr_pool *pool;
};
typedef struct fbr_init_result fbr_init_result_t;

/**
 * @brief Calculates the min bytes required by a pool with the provided options.
 *
 * This should be used when allocating a buffer that will be provided to
 * @ref fbr_init. If a static buffer is to be used then you should use this
 * function to confirm it is of adequate size.
 *
 * @warning Be careful when using a static buffer with a size based on a
 *          previous result of this function invocation, especially when
 *          using Fiber as a shared library. `fbr_pool_t` is an opaque
 *          structure and its size requirements can change, even if the
 *          major and minor versions have not changed. For this reason, it is
 *          recommended that you do not rely on the result after the program
 *          exits.
 *
 * @param options Pool initialization options to be passed to @ref fbr_init.
 *
 * @returns The minimum size the pool created from the given options will require
 *          or 0 if the options were invalid.
 */
size_t fbr_buffer_size_min(const fbr_init_options_t *options);

/**
 * @brief Initialize a new thread pool.
 *
 * @param options      Options that specify the properties of the pool.
 * @param buffer       Memory buffer the pool is initialized to. `buffer` can
 *                     be `NULL` if you'd like the function to allocate it for
 *                     you using @ref fbr_init_options::allocator. If `buffer`
 *                     is not `NULL` and the queue doesn't require an allocator,
 *                     @ref fbr_init_options::allocator can contain `NULL`
 *                     pointers.
 * @param buffer_size  Size of `buffer` in bytes. If `buffer` is `NULL` this
 *                     value is ignored.
 *
 * @returns An error code and a pointer to the pool. If @ref fbr_init_result::error
 *          is `FBR_EOK`, @ref fbr_init_result::pool will be a valid pointer to
 *          the thread pool. @ref fbr_init_result::pool will also be equal to
 *          `buffer` if it was not `NULL`.
 *
 * @retval FBR_EOK              Successfully created the thread pool and started
 *                              @ref fbr_init_options::thread_num threads.
 * @retval FBR_ENULL_ARG        `options` is `NULL` or one of
 *                              @ref fbr_init_options::queue_ops's function pointers
 *                              is `NULL`.
 * @retval FBR_EINVAL           `buffer` was not properly aligned.
 * @retval FBR_EINVAL           @ref fbr_init_options::thread_max is `0`,
 *                              @ref fbr_init_options::callers_max is `0`, or
 *                              @ref fbr_init_options::thread_num is greater than
 *                              @ref fbr_init_options::thread_max.
 * @retval FBR_EINVLD_SIZE      `buffer` was not `NULL` and `buffer_size` is smaller
 *                               than @ref fbr_buffer_size_min's return value.
 * @retval FBR_ENO_ALLOC        `buffer` was `NULL` and @ref fbr_allocator::malloc
 *                               or @ref fbr_allocator::free was `NULL`.
 * @retval FBR_ENO_MEM          `buffer` was `NULL` and @ref fbr_allocator::malloc
 *                               returned `NULL`.
 * @retval FBR_ENO_MEM          A thread could not be spawned because there was no
 *                              free entry for it. This is a bug.
 * @retval FBR_ETHRD_LIMIT      A system thread limit was reached.
 * @retval fbr_queue_ops::init  Initializing the queue returned an error. You should
 *                              consult the queue implementation's documentation to
 *                              see what is possible.
 */
fbr_init_result_t fbr_init(const fbr_init_options_t *options, void *buffer,
			   size_t buffer_size);

/**
 * @brief Signals all threads in the pool to exit and frees the pool.
 *
 * This function does not wait for all the jobs in the job queue to finish
 * executing. It tells the threads to exit ASAP and frees everything once
 * all the threads have exited. If you care about the current work in the
 * queue finishing, ensure @ref fbr_init_options::wait_enabled is `true`
 * and @ref fbr_wait is called prior to freeing.
 *
 * An important note is that the memory used by the pool is only freed
 * if @ref fbr_init received a `NULL` buffer. If you provided a buffer
 * to @ref fbr_init, you must free that buffer yourself.
 *
 * @param pool  Thread pool to free.
 */
void fbr_free(fbr_pool_t *pool);

/**
 * @brief Pushes a job onto the job queue to be executed later.
 *
 * Pushes a job onto the job queue if the queue has space. The contents
 * of `job` are copied, so `job` can be destroyed after the function
 * returns.
 *
 * The most common error this function returns is `FBR_EQUEUE_PUSH` which
 * occurs when @ref fbr_queue_ops::push returns `0`. You should consult
 * the documentation for the queue implementation to see any possible
 * reasons for failure, but it is most likely because the queue was full.
 * In this case, you should retry.
 *
 * @param pool  Thread pool to queue a job to.
 * @param job   Job to execute at a later time.
 *
 * @returns An error code.
 *
 * @retval FBR_EOK          The job was successfully queued.
 * @retval FBR_ENULL_ARG    `pool`, `job`, or @ref fbr_job::cb was `NULL`.
 * @retval FBR_EINVAL       The pool was marked inactive. This likely
 *                          means @ref fbr_free was called on the pool.
 * @retval FBR_EQUEUE_PUSH  @ref fbr_queue_ops::push returned `0`.
 */
fbr_errno_t fbr_job_push(fbr_pool_t *pool, const fbr_job_t *job);

/**
 * @brief Wait until all jobs in the queue finish or all threads exit.
 *
 * Because this waits until all jobs have finished in the queue, this
 * function should not be called by a job. Doing so _will_ result in
 * deadlock.
 *
 * @param pool  Thread pool to wait on.
 *
 * @returns An error code.
 *
 * @retval FBR_EOK        The thread pool reached a state where either
 *                          1. No threads were in the pool.
 *                          2. The queue was empty.
 *                        after `fbr_wait` was called.
 * @retval FBR_ENULL_ARG  `pool` was `NULL`.
 * @retval FBR_ENOTSUP    @ref fbr_init_options::wait_enable was `false`.
 * @retval FBR_EINVAL     The pool was marked inactive. This likely
 *                        means @ref fbr_free was called on the pool.
 * @retval FBR_ENOMEM     A hazard pointer entry could not be allocated.
 *                        This likely means @ref fbr_init_options::thread_max
 *                        or @ref fbr_init_options::callers_max were too
 *                        small.
 * @retval FBR_ENOMEM     A waiter entry could not be allocated. This likely
 *                        means either
 *                          1. @ref fbr_init_options::thread_max or
 *                             @ref fbr_init_options::callers_max were too
 *                             small.
 *                          2. The waiter entries are being created very
 *                             frequently and cannot be reclaimed fast
 *                             enough.
 *                        The second case is a bug because the number of
 *                        waiter entries allocated should be enough to handle
 *                        that case assuming (1) holds.
 */
fbr_errno_t fbr_wait(fbr_pool_t *pool);

/**
 * @brief Wait until the job specified by `job_id` completes.
 *
 * Unlike @ref fbr_wait this function is safe to call from within a job. The
 * only condition is that you add @ref fbr_init_options::thread_max to
 * @ref fbr_init_options::callers_max on initialization if you plan to
 * do this.
 *
 * @param pool    Thread pool which contains the job.
 * @param job_id  ID of the job to wait on. If `job_id` doesn't correspond
 *                to a real job the function will return immediately.
 *
 * @returns An error code.
 *
 * @retval FBR_EOK        The job with the ID `job_id` finished executing or
 *                        never existed.
 * @retval FBR_ENULL_ARG  `pool` was `NULL`.
 * @retval FBR_ENOTSUP    @ref fbr_init_options::wait_job_enable was `false`.
 * @retval FBR_EINVAL     The pool was marked inactive. This likely
 *                        means @ref fbr_free was called on the pool.
 * @retval FBR_ENOMEM     A hazard pointer entry could not be allocated.
 *                        This likely means @ref fbr_init_options::thread_max
 *                        or @ref fbr_init_options::callers_max were too
 *                        small.
 * @retval FBR_ENOMEM     A job waiter entry could not be allocated. This likely
 *                        means either
 *                          1. @ref fbr_init_options::thread_max or
 *                             @ref fbr_init_options::callers_max were too
 *                             small.
 *                          2. The job waiter entries are being created very
 *                             frequently and cannot be reclaimed fast
 *                             enough.
 *                        The second case is a bug because the number of
 *                        waiter entries allocated should be enough to handle
 *                        that case assuming (1) holds.
 */
fbr_errno_t fbr_wait_job(fbr_pool_t *pool, uint64_t job_id);

/**
 * @brief The calling thread joins the pool and starts executing jobs.
 *
 * This function allows any thread to join the pool and start executing
 * jobs. The calling thread will be _owned_ by the pool after it calls
 * this function. You should _not_ cancel the thread; it must be canceled
 * by calling @ref fbr_thread_remove. I may add a method to remove specific
 * external threads in the future.
 *
 * @param pool       Thread pool to join.
 * @param thread_id  An ID for the current thread. This should not conflict
 *                   with other user threads in the current pool.
 *
 * @returns An error code.
 *
 * @retval FBR_EOK        The thread joined the pool, did some work, then
 *                        left the pool for some reason or another.
 * @retval FBR_ENULL_ARG  `pool` was `NULL`.
 * @retval FBR_EINVAL     The pool was marked inactive. This likely
 *                        means @ref fbr_free was called on the pool.
 * @retval FBR_ENOMEM     A thread entry could not be allocated. This likely
 *                        means @ref fbr_init_options::thread_max was reached.
 */
fbr_errno_t fbr_thread_join_pool(fbr_pool_t *pool, uint64_t thread_id);

/**
 * @brief Add `*thread_num` threads to the pool.
 *
 * @param pool        Thread pool to add to.
 * @param thread_num  Pointer to the number of threads to add. The actual
 *                    number of threads started is placed in this variable.
 *
 * @returns An error code and the number of threads started via `thread_num`.
 *
 * @retval FBR_EOK          `*thread_num` threads were successfully added
 *                          to the pool.
 * @retval FBR_ENULL_ARG    `pool` was `NULL`.
 * @retval FBR_EINVAL       The pool was marked inactive. This likely
 *                          means @ref fbr_free was called on the pool.
 * @retval FBR_ETHRD_LIMIT  A system thread limit was reached.
 * @retval FBR_ENO_RSC      The number of threads actually started did not
 *                          equal `*thread_num` due to a resource limit.
 */
fbr_errno_t fbr_thread_add(fbr_pool_t *pool, uint32_t *thread_num);

/**
 * @brief Remove `thread_num` threads from the pool.
 *
 * The threads are not immediately canceled. They finish whatever job
 * they are currently executing and then exit. Because this is an
 * asynchronous process, this function may return before the threads
 * are killed.
 *
 * This function has some strange behavior when `thread_num` is greater
 * than the number of threads in the pool. The number of threads
 * "to kill" doesn't altered unless this function is called or a thread
 * exits. This means if you pass `UINT32_MAX` as `thread_num` then
 * the pool with kill all the current threads, `tcurr` and
 * `UINT32_MAX - tcurr` more.
 *
 * @param pool        Thread pool to remove threads from.
 * @param thread_num  Number of threads to kill.
 *
 * @returns An error code.
 *
 * @retval FBR_EOK          Successfully posted the intent to remove
 *                          `thread_num` threads.
 * @retval FBR_ENULL_ARG    `pool` was `NULL`.
 * @retval FBR_EINVAL       The pool was marked inactive. This likely
 *                          means @ref fbr_free was called on the pool.
 */
fbr_errno_t fbr_thread_remove(fbr_pool_t *pool, uint32_t thread_num);

/**
 * @brief Returns the number of threads currently in the pool.
 */
uint32_t fbr_thread_num(const fbr_pool_t *pool);

/**
 * @brief Returns the number of threads currently executing jobs in the pool.
 */
uint32_t fbr_thread_working(const fbr_pool_t *pool);

/**
 * @brief Returns the number of jobs waiting in the job queue.
 */
uint32_t fbr_jobs_pending(const fbr_pool_t *pool);

/**
 * @brief Returns the maximum number of threads supported by the pool.
 */
uint32_t fbr_thread_max(const fbr_pool_t *pool);

/**
 * @brief Returns the maximum number of callers supported by the pool.
 */
uint32_t fbr_callers_max(const fbr_pool_t *pool);

#endif /* FBR_H */
