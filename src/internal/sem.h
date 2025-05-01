/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_SEMAPHORE_H
#define _FIBER_SEMAPHORE_H

#if defined(FIBER_BUILD_OPT_SEMAPHORE_IMPL_POSIX)
#include <semaphore.h>
#include <unistd.h>
typedef sem_t fiber_semaphore;
#else
#error "No semaphore implementation specified."
typedef void fiber_semaphore;
#endif /* FIBER_LOCK_SEMAPHORE_POSIX */

/* Initialize a fiber_semaphore. Attempting to initialize an initailized semaphore
 * results in undefined behavior.
 * @param sem -> Pointer to the fiber_semaphore to initialize.
 * @param initial_value -> The initial value of the semaphore.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @error FBR_LOCK_ESEM_RNG -> initial_value was greater than the maximum semaphore value.
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
 * @error FBR_LOCK_EINTR -> An internal fiber error code that indicates the call was interrupted
 * by something (most likely a signal handler). The caller should retry if this is returned.
 */
int fiber_sem_wait(fiber_semaphore *sem);

/* Attempts waits on an initialized fiber_semaphore. If the value of the semaphore is > 0,
 * the semaphore is decremented and the function returns immediately. If the value of
 * the semaphore is <= 0, the function returns immediately with an error code.
 * @param sem -> Pointer to the fiber_semaphore to wait on.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @error FBR_LOCK_EINTR -> An internal fiber error code that indicates the call was interrupted
 * by something (most likely a signal handler). The caller should retry if this is returned.
 * @error FBR_LOCK_EAGAIN -> An internal fiber error code that indicates the value of the semaphore
 * was <= 0 and could not be acquired.
 */
int fiber_sem_trywait(fiber_semaphore *sem);

/* Increments the value of an initialzed fiber_semaphore possibly waking a thread blocking in
 * fiber_sem_wait.
 * @param sem -> Pointer to the fiber_semaphore to post.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @error FBR_LOCK_ESEM_RNG -> Posting the semaphore would cause an overflow.
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

#if defined(FIBER_LOCK_SEMAPHORE_INTERCEPT)
extern int (*fiber_sem_init_fn_ptr)(fiber_semaphore *, unsigned int);
extern int (*fiber_sem_destroy_fn_ptr)(fiber_semaphore *);
extern int (*fiber_sem_wait_fn_ptr)(fiber_semaphore *);
extern int (*fiber_sem_trywait_fn_ptr)(fiber_semaphore *);
extern int (*fiber_sem_post_fn_ptr)(fiber_semaphore *);
extern int (*fiber_sem_getvalue_fn_ptr)(fiber_semaphore *, int *);
#endif

#endif /* _FIBER_SEMAPHORE_H */
