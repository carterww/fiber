/* See LICENSE file for copyright and license details. */

#ifndef _FBR_THREAD_H
#define _FBR_THREAD_H

#include <fbr_errno.h>

#if defined(FIBER_BUILD_OPT_THREAD_IMPL_POSIX)
#include <pthread.h>
typedef pthread_t tid_t;
#else
#error THREADING_LIB was not set to a valid value in config.mk
#endif /* FIBER_THREADING_LIB_PTHREAD */

/* Creates and starts a new thread. The thread's id is placed in thread_id if the
 * thread was created successfully.
 * @param thread_id -> Pointer to a tid which receives the thread's id info.
 * @param runner -> Function that the thread should execute.
 * @param arg -> The sole argument passed to the runner.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @error FBR_ETHRD_LIMIT -> A new thread could not be created because the limit was reached.
 * This limit could be from a system policy, insufficient resources, etc.
 */
fbr_errno_t fbr_thread_create(tid_t *thread_id, void *(*entry)(void *),
			      void *arg);

/* Exits a thread. This function should only be called within a thread created by
 * fbr_thread_create. Attempting to exit from a process/thread not created with
 * fbr_thread_create is undefined behavior.
 * @param ret_val -> Pointer to the thread's return value.
 */
void fbr_thread_exit(void *ret_val);

/* Detaches a thread created with fbr_thread_create. This indicates that the thread
 * should not be joined and its resources can be freed immediately after exiting.
 * @param thread_id -> The id of the thread to detach.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @note As of now, there are no error codes returned by this function. All error cases
 * indicate a bug so we panic.
 */
fbr_errno_t fbr_thread_detach(const tid_t *thread_id);

/* Waits for a thread created with fbr_thread_create to terminate. After this function
 * returns, it is guaranteed that the thread has terminated. The thread's return value
 * is placed in ret_val. Calling this function on a joined thread results in undefined
 * behavior.
 * @param thread_id -> The id of the thread to join.
 * @param ret_val -> Return value of the thread. The return value (type of void *) is
 * placed in ret_val. If ret_val is NULL, the return value of the thread is not returned.
 * @note As of now, there are no error codes returned by this function. All error cases
 * indicate a bug so we panic.
 */
fbr_errno_t fbr_thread_join(const tid_t *thread_id, void **ret_val);

/* Allows a thread created by fbr_thread_create to enable cancelation. After this
 * call, the thread will not block any cancelation request from fbr_thread_cancel.
 * Calling this function from outside a thread created with fbr_thread_create results in
 * undefined behavior.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @note As of now, there are no error codes returned by this function. All error cases
 * indicate a bug so we panic.
 */
fbr_errno_t fbr_thread_cancel_enable(void);

/* Allows a thread created by fbr_thread_create to disable cancelation. After this
 * call, the thread will block any cancelation request from fbr_thread_cancel.
 * Calling this function from outside a thread created with fbr_thread_create results in
 * undefined behavior.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @note As of now, there are no error codes returned by this function. All error cases
 * indicate a bug so we panic.
 */
fbr_errno_t fbr_thread_cancel_disable(void);

/* Sets the cancelability type of the calling thread. Calling this function from
 * outside a thread created with fbr_thread_create results in undefined behavior.
 * @param cancel_type -> Can be any of the following options:
 *     - FIBER_THREAD_CANCEL_DEFERRED: Defer the threads cancelation until a viable
 *       "cancelation point."
 *     - FIBER_THREAD_CANCEL_ASYNCHRONOUS: Cancel the thread ASAP.
 * If your platform is missing some of these options or has more options, do not
 * worry. A thread's cancelation type will not change Fiber's behavior. All that
 * matters is that the thread gets canceled and the cleanup routines are executed
 * upon cancelation.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @note As of now, there are no error codes returned by this function. All error cases
 * indicate a bug so we panic.
 */
fbr_errno_t fbr_thread_cancel_type_set(int cancel_type);

/* Sends a cancelation request to a fbr_thread with the id thread_id. The thread
 * may not be canceled immediately depending on the thread's cancel state and type.
 * @param thread_id -> The id of the thread to cancel.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @note As of now, there are no error codes returned by this function. All error cases
 * indicate a bug so we panic.
 */
fbr_errno_t fbr_thread_cancel(const tid_t *thread_id);

#if defined(FIBER_BUILD_OPT_THREAD_IMPL_POSIX)
/* Pushes a cleanup routine that should be executed once the thread calls fbr_thread_exit
 * or the thread is canceled by fbr_thread_cancel. The cleanup routines should be stored
 * on a stack so they run in the opposite order they were pushed (LIFO). Calling this function
 * from outside a thread created with fbr_thread_create results in undefined behavior.
 * @param void (*cleanup_routine)(void *) -> The function to be popped from the thread's
 * cleanup stack and executed.
 * @param void *arg -> The argument passed to the cleanup_routine.
 * @note The user must call fbr_thread_cleanup_push and fbr_thread_cleanup_pop in a matching
 * pair or else a syntax error will occur.
 * @note POSIX implements these as macros so the user must put the cleanup_push and cleanup_pop
 * together. This is smart because it ensures they are both called but it is icky. Because of
 * this, I am stuck implementing the same thing.
 */
#define fbr_thread_cleanup_push(cleanup_routine, arg) \
	pthread_cleanup_push(cleanup_routine, arg)

/* Pops a cleanup routine from the cleanup stack and executes it depending on the
 * execute param. Calling this function from outside a thread created with fbr_thread_create
 * results in undefined behavior.
 * @param fbr_errno_t execute -> A boolean flag that indicates whether the routine should be executed
 * after it is popped from the cleanup stack.
 * @note The user must call fbr_thread_cleanup_push and fbr_thread_cleanup_pop in a matching
 * pair or else a syntax error will occur.
 */
#define fbr_thread_cleanup_pop(execute) pthread_cleanup_pop(execute)

#define FBR_THREAD_CANCEL_DEFERRED (PTHREAD_CANCEL_DEFERRED)
#define FBR_THREAD_CANCEL_ASYNCHRONOUS (PTHREAD_CANCEL_ASYNCHRONOUS)

#endif /* FIBER_THREADING_LIB_PTHREAD */

#if defined(FIBER_THREADING_INTERCEPT)
extern fbr_errno_t (*fbr_thread_create_fn_ptr)(tid_t *, fiber_job_function_t,
					       void *);
extern void (*fbr_thread_exit_fn_ptr)(void *);
extern fbr_errno_t (*fbr_thread_detach_fn_ptr)(const tid_t *);
extern fbr_errno_t (*fbr_thread_join_fn_ptr)(const tid_t *, void **);
extern fbr_errno_t (*fbr_thread_cancel_enable_fn_ptr)(void);
extern fbr_errno_t (*fbr_thread_cancel_disable_fn_ptr)(void);
extern fbr_errno_t (*fbr_thread_cancel_type_set_fn_ptr)(int);
extern fbr_errno_t (*fbr_thread_cancel_fn_ptr)(const tid_t *);
#endif

#endif /* _FBR_THREAD_H */
