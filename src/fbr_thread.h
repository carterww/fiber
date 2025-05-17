/* See LICENSE file for copyright and license details. */

#ifndef _FBR_THREAD_H
#define _FBR_THREAD_H

#include <ck_pr.h>
#include <stdbool.h>

#include <fbr_errno.h>

#include "fbr_platform.h"

#if defined(FBR_OS_LINUX) || defined(FBR_OS_FREEBSD)
#define FBR_THREAD_PTHREAD
#include <pthread.h>
typedef pthread_t tid_t;
#else
#error "No thread implementation provided"
#endif

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
			      void *arg, size_t stack_size);

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

#if defined(FBR_THREAD_PTHREAD)
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

inline static tid_t fbr_thread_self(void)
{
	return pthread_self();
}

inline static bool fbr_thread_tid_equal(const tid_t *t1, const tid_t *t2)
{
	return pthread_equal(*t1, *t2) ? true : false;
}

#endif /* FBR_THREAD_PTHREAD */
#endif /* _FBR_THREAD_H */
