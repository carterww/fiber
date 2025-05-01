/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_MUTEX_H
#define _FIBER_MUTEX_H

#if defined(FIBER_BUILD_OPT_MUTEX_IMPL_POSIX)
#include <pthread.h>
#include <unistd.h>
typedef pthread_mutex_t fiber_mutex;
#else
#error "No mutex implementation specified."
typedef void fiber_mutex;
#endif /* FIBER_LOCK_MUTEX_POSIX */

/* Initialize a fiber_mutex. This function should only be called on a uninitialized mutex.
 * If it is called on an initialized mutex, it may fail depending on the implementation.
 * @param mut -> Pointer to the fiber_mutex to initialize.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @error FBR_LOCK_ENO_RSC -> The system did not have resources to initialize the mutex.
 * @error FBR_LOCK_EPTHRD_PERM -> The process does not have permission to initialize a mutex.
 * @error FBR_LOCK_ENOMEM -> The system did not have sufficient memory to initialize the mutex.
 */
int fiber_mutex_init(fiber_mutex *mut);

/* Destroys an initialized fiber_mutex. This function should not be called on an uninitialized
 * or locked mutex. Both cases will result in undefined behavior.
 * @param mut -> Pointer to the fiber_mutex to destroy.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @error FBR_LOCK_EBUSY -> Tried to destroy a locked mutex.
 */
int fiber_mutex_destroy(fiber_mutex *mut);

/* Locks an initialized fiber_mutex. This function should not be called on an uninitialized
 * or locked mutex held by the caller. Both cases will result in undefined behavior.
 * @param mut -> Pointer to the fiber_mutex to lock.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @note As of now, there are no error codes returned by this function. All error cases
 * indicate a bug so we panic.
 */
int fiber_mutex_lock(fiber_mutex *mut);

/* Unlocks an initialized fiber_mutex. This function should not be called on an uninitialized
 * or mutex that has already been unlocked. Both cases will result in undefined behavior.
 * @param mut -> Pointer to the fiber_mutex to unlock.
 * @returns -> 0 if the call was successful, an error otherwise.
 * @note As of now, there are no error codes returned by this function. All error cases
 * indicate a bug so we panic.
 */
int fiber_mutex_unlock(fiber_mutex *mut);

#if defined(FIBER_LOCK_MUTEX_INTERCEPT)
extern int (*fiber_mutex_init_fn_ptr)(fiber_mutex *);
extern int (*fiber_mutex_destroy_fn_ptr)(fiber_mutex *);
extern int (*fiber_mutex_lock_fn_ptr)(fiber_mutex *);
extern int (*fiber_mutex_unlock_fn_ptr)(fiber_mutex *);
#endif

#endif /* _FIBER_MUTEX_H */
