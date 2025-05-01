#define _POSIX_C_SOURCE (199309L)

#include <errno.h>

#include "debug.h"
#include "fiber/fiber.h"
#include "sem.h"

#if defined(FIBER_LOCK_SEMAPHORE_INTERCEPT)
#define fiber_sem_init _fiber_sem_init
#define fiber_sem_destroy _fiber_sem_destroy
#define fiber_sem_wait _fiber_sem_wait
#define fiber_sem_trywait _fiber_sem_trywait
#define fiber_sem_post _fiber_sem_post
#define fiber_sem_getvalue _fiber_sem_getvalue
#endif

int fiber_sem_init(fiber_semaphore *sem, unsigned int initial_value)
{
	int res;

	fiber_assert(sem != NULL);
	res = sem_init(sem, 0, initial_value);

	if (res == 0) {
		return res;
	}

	/* System does not support shared process semaphores. We don't use
         * those so this shouldn't be possible.
         */
	fiber_assert(errno != ENOSYS);

	switch (errno) {
	case EINVAL: /* initial_value exceeds semaphore's max */
		return FBR_ESEM_RNG;
	default:
		panic(1);
	}
}

int fiber_sem_destroy(fiber_semaphore *sem)
{
	int res;

	fiber_assert(sem != NULL);
	res = sem_destroy(sem);

	if (res == 0) {
		return res;
	}

	fiber_assert(errno != EINVAL); /* sem is not a valid semaphore */

	switch (errno) {
	default:
		panic(1);
	}
}

int fiber_sem_wait(fiber_semaphore *sem)
{
	int res;

	fiber_assert(sem != NULL);
	res = sem_wait(sem);

	if (res == 0) {
		return res;
	}

	fiber_assert(errno != EINVAL); /* sem is not a valid semaphore */

	switch (errno) {
	case EINTR: /* Call interrupted by signal */
		return FBR_EINTR;
	default:
		panic(1);
	}
}


int fiber_sem_trywait(fiber_semaphore *sem)
{
	int res;

	fiber_assert(sem != NULL);
	res = sem_trywait(sem);

	if (res == 0) {
		return res;
	}

	fiber_assert(errno != EINVAL); /* sem is not a valid semaphore */

	switch (errno) {
	case EINTR: /* Call interrupted by signal */
		return FBR_EINTR;
	case EAGAIN: /* Could not wait on semaphore without blocking */
		return FBR_EAGAIN;
	default:
		panic(1);
	}
}

int fiber_sem_post(fiber_semaphore *sem)
{
	int res;

	fiber_assert(sem != NULL);
	res = sem_post(sem);

	if (res == 0) {
		return res;
	}

	fiber_assert(errno != EINVAL); /* sem is not a valid semaphore */

	switch (errno) {
	case EOVERFLOW: /* Max value for semaphore would be exceeded */
		return FBR_ESEM_RNG;
	default:
		panic(1);
	}
}

int fiber_sem_getvalue(fiber_semaphore *sem, int *value_out)
{
	int res;

	fiber_assert(sem != NULL);
	fiber_assert(value_out != NULL);
	res = sem_getvalue(sem, value_out);

	if (res == 0) {
		return res;
	}

	fiber_assert(errno != EINVAL); /* sem is not a valid semaphore */

	switch (errno) {
	default:
		panic(1);
	}
}

#if defined(FIBER_LOCK_SEMAPHORE_INTERCEPT)
#undef fiber_sem_init
#undef fiber_sem_destroy
#undef fiber_sem_wait
#undef fiber_sem_post
#undef fiber_sem_trywait
#undef fiber_sem_getvalue
int (*fiber_sem_init_fn_ptr)(fiber_semaphore *, unsigned int) = _fiber_sem_init;
int (*fiber_sem_destroy_fn_ptr)(fiber_semaphore *) = _fiber_sem_destroy;
int (*fiber_sem_wait_fn_ptr)(fiber_semaphore *) = _fiber_sem_wait;
int (*fiber_sem_trywait_fn_ptr)(fiber_semaphore *) = _fiber_sem_trywait;
int (*fiber_sem_post_fn_ptr)(fiber_semaphore *) = _fiber_sem_post;
int (*fiber_sem_getvalue_fn_ptr)(fiber_semaphore *,
		int *) = _fiber_sem_getvalue;
#endif
