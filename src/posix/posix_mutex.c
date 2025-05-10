#define _POSIX_C_SOURCE (199506L)

#include <errno.h>

#include <fbr_errno.h>

#include "fbr_debug.h"
#include "fbr_mutex.h"

#if defined(FIBER_LOCK_MUTEX_INTERCEPT)
#define fbr_mutex_init _fbr_mutex_init
#define fbr_mutex_destroy _fbr_mutex_destroy
#define fbr_mutex_lock _fbr_mutex_lock
#define fbr_mutex_unlock _fbr_mutex_unlock
#endif

fbr_errno_t fbr_mutex_init(fbr_mutex_t *mut)
{
	int res;

	fbr_assert(mut != NULL);
	res = pthread_mutex_init(mut, NULL);

	if (res == 0) {
		return FBR_EOK;
	}

	fbr_assert(res != EINVAL); /* This only occurs if attr is invalid */
	fbr_assert(res != EBUSY); /* Attempted to reinitialize a mutex */

	switch (res) {
	case EAGAIN: /* System did not have resource to init mutx (excluding mem). */
		return FBR_ENO_RSC;
	case EPERM: /* Does not have permission to init mutex */
		return FBR_EPTHRD_PERM;
	case ENOMEM: /* No memory */
		return FBR_ENOMEM;
	default:
		fbr_panic(res);
		return FBR_EGENERIC;
	}
}

fbr_errno_t fbr_mutex_destroy(fbr_mutex_t *mut)
{
	int res;

	fbr_assert(mut != NULL);
	res = pthread_mutex_destroy(mut);

	if (res == 0) {
		return FBR_EOK;
	}

	fbr_assert(res != EINVAL); /* Mutex is invalid */

	switch (res) {
	case EBUSY: /* Trying to destroy a locked mutex */
		return FBR_EBUSY;
	default:
		fbr_panic(res);
		return FBR_EGENERIC;
	}
}

fbr_errno_t fbr_mutex_lock(fbr_mutex_t *mut)
{
	int res;

	fbr_assert(mut != NULL);
	res = pthread_mutex_lock(mut);

	if (res == 0) {
		return FBR_EOK;
	}

	/* Invalid mutex or PTHREAD_PRIO_PROTECT issue (N/A here) */
	fbr_assert(res != EINVAL);
	/* Max number of recursive locks exceeded */
	fbr_assert(res != EAGAIN);

	switch (res) {
	case EDEADLK: /* Caller already owns mutex */
		return FBR_EOK;
	default:
		fbr_panic(res);
		return FBR_EGENERIC;
	}
}

fbr_errno_t fbr_mutex_unlock(fbr_mutex_t *mut)
{
	int res;

	fbr_assert(mut != NULL);
	res = pthread_mutex_unlock(mut);

	if (res == 0) {
		return FBR_EOK;
	}

	/* Invalid mutex */
	fbr_assert(res != EINVAL);
	/* Max number of recursive locks exceeded */
	fbr_assert(res != EAGAIN);
	/* Caller does not own the mutex */
	fbr_assert(res != EPERM);

	switch (res) {
	default:
		fbr_panic(res);
		return FBR_EGENERIC;
	}
}

#if defined(FIBER_LOCK_MUTEX_INTERCEPT)
#undef fbr_mutex_init
#undef fbr_mutex_destroy
#undef fbr_mutex_lock
#undef fbr_mutex_unlock
fbr_errno_t (*fbr_mutex_init_fn_ptr)(fbr_mutex_t *) = _fbr_mutex_init;
fbr_errno_t (*fbr_mutex_destroy_fn_ptr)(fbr_mutex_t *) = _fbr_mutex_destroy;
fbr_errno_t (*fbr_mutex_lock_fn_ptr)(fbr_mutex_t *) = _fbr_mutex_lock;
fbr_errno_t (*fbr_mutex_unlock_fn_ptr)(fbr_mutex_t *) = _fbr_mutex_unlock;
#endif
