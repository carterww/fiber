/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_THREADING_H
#define _FIBER_THREADING_H

#include <errno.h>

#include "fiber.h"

#if FIBER_USE_PTHREADS != 0

#include <pthread.h>
#include <semaphore.h>

typedef pthread_t tid;
typedef pthread_mutex_t fiber_mutex;
typedef sem_t fiber_semaphore;

/* Pushes a cleanup routine that should be executed once the thread calls fiber_thread_exit
 * or the thread is canceled by fiber_thread_cancel. The cleanup routines should be stored
 * on a stack so they run in the opposite order they were pushed (LIFO). Calling this function
 * from outside a thread created with fiber_thread_create results in undefined behavior.
 * @param void (*cleanup_routine)(void *) -> The function to be popped from the thread's
 * cleanup stack and executed.
 * @param void *arg -> The argument passed to the cleanup_routine.
 * @note The user must call fiber_thread_cleanup_push and fiber_thread_cleanup_pop in a matching
 * pair or else a syntax error will occur.
 * @note POSIX implements these as macros so the user must put the cleanup_push and cleanup_pop
 * together. This is smart because it ensures they are both called but it is icky. Because of
 * this, I am stuck implementing the same thing.
 */
#define fiber_thread_cleanup_push(cleanup_routine, arg) \
	pthread_cleanup_push(cleanup_routine, arg)

/* Pops a cleanup routine from the cleanup stack and executes it depending on the
 * execute param. Calling this function from outside a thread created with fiber_thread_create
 * results in undefined behavior.
 * @param int execute -> A boolean flag that indicates whether the routine should be executed
 * after it is popped from the cleanup stack.
 * @note The user must call fiber_thread_cleanup_push and fiber_thread_cleanup_pop in a matching
 * pair or else a syntax error will occur.
 */
#define fiber_thread_cleanup_pop(execute) pthread_cleanup_pop(execute)

#define FIBER_THREAD_CANCEL_DEFERRED (PTHREAD_CANCEL_DEFERRED)
#define FIBER_THREAD_CANCEL_ASYNCHRONOUS (PTHREAD_CANCEL_ASYNCHRONOUS)

#else
#error "FIBER_USE_PTHREADS was disabled in fiber.h but there are no alternative typedefs and macros provided."
#endif /* FIBER_USE_PTHREADS */

/** Semaphore functions **/

/* Initialize a fiber_semaphore. Attempting to initialize an initailized semaphore
 * results in undefined behavior.
 * @param sem -> Pointer to the fiber_semaphore to initialize.
 * @param initial_value -> The initial value of the semaphore.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @error FBR_ESEM_RNG -> initial_value was greater than the maximum semaphore value.
 */
int fiber_sem_init(fiber_semaphore *sem, unsigned int initial_value);

/* Destroys a fiber_semaphore. Attempting to destroy an uninitalized semaphore or a semaphore
 * callers are currently waiting on will result in undefined behavior.
 * @param sem -> Pointer to the fiber_semaphore to destroy.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @note As of now, there are no error codes returned by this function. All error cases
 * indicate a bug so we panic.
 */
int fiber_sem_destroy(fiber_semaphore *sem);

/* Waits on an initialized fiber_semaphore. If the value of the semaphore is > 0, the semaphore
 * is decremented and the function returns immediately. If the value of the semaphore is <= 0,
 * the function blocks until other thread(s) call fiber_sem_post.
 * @param sem -> Pointer to the fiber_semaphore to wait on.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @error FBR_ETHREADING_EINTR -> An internal fiber error code that indicates the call was interrupted
 * by something (most likely a signal handler). The caller should retry if this is returned.
 */
int fiber_sem_wait(fiber_semaphore *sem);

/* Attempts waits on an initialized fiber_semaphore. If the value of the semaphore is > 0,
 * the semaphore is decremented and the function returns immediately. If the value of
 * the semaphore is <= 0, the function returns immediately with an error code.
 * @param sem -> Pointer to the fiber_semaphore to wait on.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @error FBR_ETHREADING_EINTR -> An internal fiber error code that indicates the call was interrupted
 * by something (most likely a signal handler). The caller should retry if this is returned.
 * @error FBR_ETHREADING_EAGAIN -> An internal fiber error code that indicates the value of the semaphore
 * was <= 0 and could not be acquired.
 */
int fiber_sem_trywait(fiber_semaphore *sem);

/* Increments the value of an initialzed fiber_semaphore possibly waking a thread blocking in
 * fiber_sem_wait.
 * @param sem -> Pointer to the fiber_semaphore to post.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @note As of now, there are no error codes returned by this function. All error cases
 * indicate a bug so we panic.
 */
int fiber_sem_post(fiber_semaphore *sem);

/* Gets the current value of an initialzed fiber_semaphore.
 * @param sem -> Pointer to the fiber_semaphore to get the current value. sem's memory
 * should not overlap with value_out's (restrict).
 * @param value_out -> Pointer to the int that will receive the semaphore's value.
 * value_out's memory should not overlap with sems's (restrict). If the semaphore's value is
 * <= 0, value_out may be given 0 or a negative number. POSIX permits either when the semaphore's
 * true value is <= 0.
 * @returns -> 0 if the call was successful, an error otherwise. 
 * @note As of now, there are no error codes returned by this function. All error cases
 * indicate a bug so we panic.
 */
int fiber_sem_getvalue(fiber_semaphore *sem, int *value_out);

/** Mutex functions **/

/* Initialize a fiber_mutex. This function should only be called on a uninitalized mutex.
 * If it is called on an initialized mutex, it may fail depending on the implementation.
 * @param mut -> Pointer to the fiber_mutex to initialize.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @error FBR_ENO_RSC -> The system did not have resources to initialize the mutex.
 * @error FBR_EPTHRD_PERM -> The process does not have permission to initialize a mutex.
 */
int fiber_mutex_init(fiber_mutex *mut);

/* Destroys an initialized fiber_mutex. This function should not be called on an uninitalized
 * or locked mutex. Both cases will result in undefined behavior.
 * @param mut -> Pointer to the fiber_mutex to destroy.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @note As of now, there are no error codes returned by this function. All error cases
 * indicate a bug so we panic.
 */
int fiber_mutex_destroy(fiber_mutex *mut);

/* Locks an initialized fiber_mutex. This function should not be called on an uninitalized
 * or locked mutex. Both cases will result in undefined behavior.
 * @param mut -> Pointer to the fiber_mutex to lock.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @note As of now, there are no error codes returned by this function. All error cases
 * indicate a bug so we panic.
 */
int fiber_mutex_lock(fiber_mutex *mut);

/* Unlocks an initialized fiber_mutex. This function should not be called on an uninitalized
 * or mutex that has already been unlockded. Both cases will result in undefined behavior.
 * @param mut -> Pointer to the fiber_mutex to unlock.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @note As of now, there are no error codes returned by this function. All error cases
 * indicate a bug so we panic.
 */
int fiber_mutex_unlock(fiber_mutex *mut);

/** Thread functions **/

/* Creates and starts a new thread. The thread's id is placed in thread_id if the
 * thread was created successfully.
 * @param thread_id -> Pointer to a tid which receives the thread's id info.
 * @param runner -> Function that the thread should execute.
 * @param arg -> The sole argument passed to the runner.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @error FBR_ETHRD_LIMIT -> A new thread could not be created because the limit was reached.
 * This limit could be from a system policy, insufficient resources, etc.
 */
int fiber_thread_create(tid *thread_id, void *(*runner)(void *), void *arg);

/* Exits a thread. This function should only be called within a thread created by
 * fiber_thread_create. Attempting to exit from a process/thread not created with
 * fiber_thread_create is undefined behavior.
 * @param ret_val -> Pointer to the thread's return value.
 */
void fiber_thread_exit(void *ret_val);

/* Detaches a thread created with fiber_thread_create. This indicates that the thread
 * should not be joined and its resources can be freed immediately after exiting.
 * @param thread_id -> The id of the thread to detach.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @note As of now, there are no error codes returned by this function. All error cases
 * indicate a bug so we panic.
 */
int fiber_thread_detach(const tid *thread_id);

/* Allows a thread created by fiber_thread_create to enable cancelation. After this
 * call, the thread will not block any cancelation request from fiber_thread_cancel.
 * Calling this function from outside a thread created with fiber_thread_create results in
 * undefined behavior.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @note As of now, there are no error codes returned by this function. All error cases
 * indicate a bug so we panic.
 */
int fiber_thread_cancel_enable(void);

/* Allows a thread created by fiber_thread_create to disable cancelation. After this
 * call, the thread will block any cancelation request from fiber_thread_cancel.
 * Calling this function from outside a thread created with fiber_thread_create results in
 * undefined behavior.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @note As of now, there are no error codes returned by this function. All error cases
 * indicate a bug so we panic.
 */
int fiber_thread_cancel_disable(void);

/* Sets the cancelability type of the calling thread. Calling this function from
 * outside a thread created with fiber_thread_create results in undefined behavior.
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
int fiber_thread_cancel_type_set(int cancel_type);

/* Sends a cancelation request to a fiber_thread with the id thread_id. The thread
 * may not be canceled immediately depending on the thread's cancel state and type.
 * @param thread_id -> The id of the thread to cancel.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @note As of now, there are no error codes returned by this function. All error cases
 * indicate a bug so we panic.
 */
int fiber_thread_cancel(const tid *thread_id);

/** Error codes **/

#define FBR_ETHREADING_EINTR (EINTR)
#define FBR_ETHREADING_EAGAIN (EAGAIN)

#endif /* _FIBER_THREADING_H */
