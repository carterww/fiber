/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_WORKER_H
#define _FIBER_WORKER_H

#include "fiber_internal.h"

/* Argument passed to the runner function passed to fiber_thread_create.
 * The prev member is not used by the fiber_worker_runner. fiber_workers_start
 * uses it to track all the allocated args in a list in case it needs to free them
 * on error.
 */
struct fiber_worker_thread_arg {
	struct fiber_pool *pool;
	struct fiber_thread *thread;
	struct fiber_worker_thread_arg *prev;
};

/* Starts up to threads_number fiber_threads in the pool. These threads
 * will sit idle until there are jobs to execute.
 * @param pool -> The pool the threads are started under.
 * @param threads_head -> List of threads to start.
 * @param threads_number -> The maximum number of threads the start. The
 * function will start threads until it reaches a NULL fiber_thread in
 * the list or it started threads_number fiber_threads.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @error FBR_ENO_RSC -> The malloc function attached to the pool
 * returned a NULL pointer.
 * @error FBR_ETHRD_LIMIT -> A new thread could not be created because the limit was reached.
 * This limit could be from a system policy, insufficient resources, etc.
 */
int fiber_workers_start(struct fiber_pool *pool,
			struct fiber_thread *threads_head,
			tpsize threads_number);

/* Entry function for the new worker thread.
 * @param fiber_worker_thread_arg -> Struct of the type fiber_worker_thread_arg.
 * @returns -> Only returns if the user kills the thread.
 */
void *fiber_worker_runner(void *fiber_worker_thread_arg);

/* Cancels threads_number of threads. The threads are not immediately canceled, but
 * they will be canceled when possible. This function will return only after all the
 * threads are canceled.
 * Important note: This should only be used for sets of threads that were either never
 * added to the pool or make up the entire pool.
 * @param threads_head -> The list of threads to cancel.
 * @param threads_number -> The maximum number of threads to cancel.
 */
void fiber_workers_cancel(const struct fiber_thread *threads_head,
			  tpsize threads_number);

/* Pushes a job onto the queue to wake a sleeping thread. If no threads
 * are sleeping, it does nothing.
 * @param pool -> Pool to wake a thread in.
 * @note This function is only used to handle the case where the user wishes
 * to cancel threads but all of the threads are sleeping.
 */
void fiber_worker_wake_other(const struct fiber_pool *pool);

#endif /* _FIBER_WORKER_H */
