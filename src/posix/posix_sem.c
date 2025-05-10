#define _POSIX_C_SOURCE (199309L)

#include <errno.h>

#include <fbr_errno.h>

#include "fbr_debug.h"
#include "fbr_sem.h"

#if defined(FIBER_LOCK_SEMAPHORE_INTERCEPT)
#define fbr_sem_init _fbr_sem_init
#define fbr_sem_destroy _fbr_sem_destroy
#define fbr_sem_wait _fbr_sem_wait
#define fbr_sem_trywait _fbr_sem_trywait
#define fbr_sem_post _fbr_sem_post
#define fbr_sem_getvalue _fbr_sem_getvalue
#endif

fbr_errno_t fbr_sem_init(fbr_sem_t *sem, int initial_value)
{
	int res;

	fbr_assert(sem != NULL);

	if (initial_value < 0) {
		return FBR_EINVLD_SIZE;
	}
	res = sem_init(sem, 0, (unsigned int)initial_value);

	if (res == 0) {
		return res;
	}

	/* System does not support shared process semaphores. We don't use
         * those so this shouldn't be possible.
         */
	fbr_assert(errno != ENOSYS);

	switch (errno) {
	case EINVAL: /* initial_value exceeds semaphore's max */
		return FBR_EINVLD_SIZE;
	default:
		fbr_panic(res);
		return FBR_EGENERIC;
	}
}

fbr_errno_t fbr_sem_destroy(fbr_sem_t *sem)
{
	int res;

	fbr_assert(sem != NULL);
	res = sem_destroy(sem);

	if (res == 0) {
		return res;
	}

	fbr_assert(errno != EINVAL); /* sem is not a valid semaphore */

	switch (errno) {
	default:
		fbr_panic(res);
		return FBR_EGENERIC;
	}
}

fbr_errno_t fbr_sem_wait(fbr_sem_t *sem)
{
	int res;

	fbr_assert(sem != NULL);
	res = sem_wait(sem);

	if (res == 0) {
		return res;
	}

	fbr_assert(errno != EINVAL); /* sem is not a valid semaphore */

	switch (errno) {
	case EINTR: /* Call interrupted by signal */
		return FBR_EINTR;
	default:
		fbr_panic(res);
		return FBR_EGENERIC;
	}
}

fbr_errno_t fbr_sem_trywait(fbr_sem_t *sem)
{
	int res;

	fbr_assert(sem != NULL);
	res = sem_trywait(sem);

	if (res == 0) {
		return res;
	}

	fbr_assert(errno != EINVAL); /* sem is not a valid semaphore */

	switch (errno) {
	case EINTR: /* Call interrupted by signal */
		return FBR_EINTR;
	case EAGAIN: /* Could not wait on semaphore without blocking */
		return FBR_EAGAIN;
	default:
		fbr_panic(res);
		return FBR_EGENERIC;
	}
}

fbr_errno_t fbr_sem_post(fbr_sem_t *sem)
{
	int res;

	fbr_assert(sem != NULL);
	res = sem_post(sem);

	if (res == 0) {
		return res;
	}

	fbr_assert(errno != EINVAL); /* sem is not a valid semaphore */

	switch (errno) {
	case EOVERFLOW: /* Max value for semaphore would be exceeded */
		return FBR_EINVLD_SIZE;
	default:
		fbr_panic(res);
		return FBR_EGENERIC;
	}
}

fbr_errno_t fbr_sem_getvalue(fbr_sem_t *sem, int *value_out)
{
	int res;

	fbr_assert(sem != NULL);
	fbr_assert(value_out != NULL);
	res = sem_getvalue(sem, value_out);

	if (res == 0) {
		return res;
	}

	fbr_assert(errno != EINVAL); /* sem is not a valid semaphore */

	switch (errno) {
	default:
		fbr_panic(res);
		return FBR_EGENERIC;
	}
}

#if defined(FIBER_LOCK_SEMAPHORE_INTERCEPT)
#undef fbr_sem_init
#undef fbr_sem_destroy
#undef fbr_sem_wait
#undef fbr_sem_post
#undef fbr_sem_trywait
#undef fbr_sem_getvalue
fbr_errno_t (*fbr_sem_init_fn_ptr)(fbr_sem_t *, unsigned int) = _fbr_sem_init;
fbr_errno_t (*fbr_sem_destroy_fn_ptr)(fbr_sem_t *) = _fbr_sem_destroy;
fbr_errno_t (*fbr_sem_wait_fn_ptr)(fbr_sem_t *) = _fbr_sem_wait;
fbr_errno_t (*fbr_sem_trywait_fn_ptr)(fbr_sem_t *) = _fbr_sem_trywait;
fbr_errno_t (*fbr_sem_post_fn_ptr)(fbr_sem_t *) = _fbr_sem_post;
fbr_errno_t (*fbr_sem_getvalue_fn_ptr)(fbr_sem_t *, int *) = _fbr_sem_getvalue;
#endif
