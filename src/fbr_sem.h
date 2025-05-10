/* See LICENSE file for copyright and license details. */

#ifndef _FBR_SEMAPHORE_H
#define _FBR_SEMAPHORE_H

#include <fbr_errno.h>

#if defined(FIBER_BUILD_OPT_SEMAPHORE_IMPL_POSIX)
#include <semaphore.h>
#include <unistd.h>
typedef sem_t fbr_sem_t;
#else
#error "No valid semaphore implementation specified."
#endif /* FIBER_LOCK_SEMAPHORE_POSIX */

fbr_errno_t fbr_sem_init(fbr_sem_t *sem, int initial_value);

fbr_errno_t fbr_sem_destroy(fbr_sem_t *sem);

fbr_errno_t fbr_sem_wait(fbr_sem_t *sem);

fbr_errno_t fbr_sem_trywait(fbr_sem_t *sem);

fbr_errno_t fbr_sem_post(fbr_sem_t *sem);

fbr_errno_t fbr_sem_getvalue(fbr_sem_t *sem, int *value_out);

#if defined(FIBER_LOCK_SEMAPHORE_INTERCEPT)
extern fbr_errno_t (*fbr_sem_init_fn_ptr)(fbr_sem_t *, int);
extern fbr_errno_t (*fbr_sem_destroy_fn_ptr)(fbr_sem_t *);
extern fbr_errno_t (*fbr_sem_wait_fn_ptr)(fbr_sem_t *);
extern fbr_errno_t (*fbr_sem_trywait_fn_ptr)(fbr_sem_t *);
extern fbr_errno_t (*fbr_sem_post_fn_ptr)(fbr_sem_t *);
extern fbr_errno_t (*fbr_sem_getvalue_fn_ptr)(fbr_sem_t *, int *);
#endif

#endif /* _FBR_SEMAPHORE_H */
