#define _POSIX_C_SOURCE (199506L)

#include <errno.h>

#include "debug.h"
#include "fiber/fiber.h"
#include "internal/mutex.h"
#include "mutex.h"

#if defined(FIBER_LOCK_MUTEX_INTERCEPT)
#define fiber_mutex_init _fiber_mutex_init
#define fiber_mutex_destroy _fiber_mutex_destroy
#define fiber_mutex_lock _fiber_mutex_lock
#define fiber_mutex_unlock _fiber_mutex_unlock
#endif

int fiber_mutex_init(fiber_mutex *mut)
{
	int res;

	fiber_assert(mut != NULL);
	res = pthread_mutex_init(mut, NULL);

	fiber_assert(res != EINVAL); /* This only occurs if attr is invalid */
	fiber_assert(res != EBUSY); /* Attempted to reinitialize a mutex */

	switch (res) {
	case 0:
		return res;
	case EAGAIN: /* System did not have resource to init mutx (excluding mem). */
		return FBR_ENO_RSC;
	case EPERM: /* Does not have permission to init mutex */
		return FBR_EPTHRD_PERM;
	case ENOMEM: /* No memory */
		return FBR_ENOMEM;
	default:
		panic(1);
	}
}

int fiber_mutex_destroy(fiber_mutex *mut)
{
	int res;

	fiber_assert(mut != NULL);
	res = pthread_mutex_destroy(mut);

	fiber_assert(res != EINVAL); /* Mutex is invalid */

	switch (res) {
	case 0:
		return res;
	case EBUSY: /* Trying to destroy a locked mutex */
		return FBR_EBUSY;
	default:
		panic(1);
	}
}

int fiber_mutex_lock(fiber_mutex *mut)
{
	int res;

	fiber_assert(mut != NULL);
	res = pthread_mutex_lock(mut);

	/* Invalid mutex or PTHREAD_PRIO_PROTECT issue (N/A here) */
	fiber_assert(res != EINVAL);
	/* Max number of recursive locks exceeded */
	fiber_assert(res != EAGAIN);

	switch (res) {
	case 0:
		return res;
	case EDEADLK: /* Caller already owns mutex */
		return 0;
	default:
		panic(1);
	}
}

int fiber_mutex_unlock(fiber_mutex *mut)
{
	int res;

	fiber_assert(mut != NULL);
	res = pthread_mutex_unlock(mut);

	/* Invalid mutex */
	fiber_assert(res != EINVAL);
	/* Max number of recursive locks exceeded */
	fiber_assert(res != EAGAIN);
	/* Caller does not own the mutex */
	fiber_assert(res != EPERM);

	switch (res) {
	case 0:
		return res;
	default:
		panic(1);
	}
}

#if defined(FIBER_LOCK_MUTEX_INTERCEPT)
#undef fiber_mutex_init
#undef fiber_mutex_destroy
#undef fiber_mutex_lock
#undef fiber_mutex_unlock
int (*fiber_mutex_init_fn_ptr)(fiber_mutex *) = _fiber_mutex_init;
int (*fiber_mutex_destroy_fn_ptr)(fiber_mutex *) = _fiber_mutex_destroy;
int (*fiber_mutex_lock_fn_ptr)(fiber_mutex *) = _fiber_mutex_lock;
int (*fiber_mutex_unlock_fn_ptr)(fiber_mutex *) = _fiber_mutex_unlock;
#endif
