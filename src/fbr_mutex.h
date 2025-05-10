/* See LICENSE file for copyright and license details. */

#ifndef _FBR_MUTEX_H
#define _FBR_MUTEX_H

#include <fbr_errno.h>

#if defined(FIBER_BUILD_OPT_MUTEX_IMPL_POSIX)
#include <pthread.h>
#include <unistd.h>
typedef pthread_mutex_t fbr_mutex_t;
#else
#error "No valid mutex implementation specified."
#endif /* FIBER_LOCK_MUTEX_POSIX */

fbr_errno_t fbr_mutex_init(fbr_mutex_t *mut);

fbr_errno_t fbr_mutex_destroy(fbr_mutex_t *mut);

fbr_errno_t fbr_mutex_lock(fbr_mutex_t *mut);

fbr_errno_t fbr_mutex_unlock(fbr_mutex_t *mut);

#if defined(FIBER_LOCK_MUTEX_INTERCEPT)
extern fbr_errno_t (*fbr_mutex_init_fn_ptr)(fbr_mutex_t *);
extern fbr_errno_t (*fbr_mutex_destroy_fn_ptr)(fbr_mutex_t *);
extern fbr_errno_t (*fbr_mutex_lock_fn_ptr)(fbr_mutex_t *);
extern fbr_errno_t (*fbr_mutex_unlock_fn_ptr)(fbr_mutex_t *);
#endif

#endif /* _FBR_MUTEX_H */
