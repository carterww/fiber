/* See LICENSE file for copyright and license details. */

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stddef.h>

#include "fiber.h"
#include "threading.h"
#include "utils.h"

/* This is here to support the threading_fault module in test/mock/threading.
 * Redefining these functions and putting them into a vtable allows us to use
 * their implementation while adding some other code before and after it. Don't
 * worry about this at all, it is just for testing purposes.
 */
#if defined(FIBER_THREADING_INTERCEPT) && defined(FIBER_BUILD_ENV_TEST)
#define fiber_sem_init __fiber_sem_init
#define fiber_sem_destroy __fiber_sem_destroy
#define fiber_sem_wait __fiber_sem_wait
#define fiber_sem_trywait __fiber_sem_trywait
#define fiber_sem_post __fiber_sem_post
#define fiber_sem_getvalue __fiber_sem_getvalue

#define fiber_mutex_init __fiber_mutex_init
#define fiber_mutex_destroy __fiber_mutex_destroy
#define fiber_mutex_lock __fiber_mutex_lock
#define fiber_mutex_unlock __fiber_mutex_unlock

#define fiber_thread_create __fiber_thread_create
#define fiber_thread_exit __fiber_thread_exit
#define fiber_thread_detach __fiber_thread_detach
#define fiber_thread_join __fiber_thread_join
#define fiber_thread_cancel_enable __fiber_thread_enable
#define fiber_thread_cancel_disable __fiber_thread_disable
#define fiber_thread_cancel_type_set __fiber_thread_cancel_type_set
#define fiber_thread_cancel __fiber_thread_cancel
#endif /* FIBER_TEST_THREADING_MOCK && FIBER_BUILD_ENV_TEST */

/** Semaphore functions **/

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
		panic(1);
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

/** Mutex functions **/

int fiber_mutex_init(fiber_mutex *mut)
{
	int res;

	fiber_assert(mut != NULL);
	res = pthread_mutex_init(mut, NULL);

	fiber_assert(res != EINVAL); /* This only occurs is attr is invalid */
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
		panic(1);
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

	fiber_assert(res != EINVAL); /* Invalid mutex */
	/* Max number of recursive locks exceeded */
	fiber_assert(res != EAGAIN);

	switch (res) {
	case 0:
		return res;
	case EPERM: /* Caller does not own the mutex */
		panic(1);
	default:
		panic(1);
	}
}

/** Thread functions **/

static int __fiber_thread_setcancelstate(int state)
{
	int res;

	fiber_assert(state == PTHREAD_CANCEL_ENABLE ||
		     state == PTHREAD_CANCEL_DISABLE);
	res = pthread_setcancelstate(state, NULL);

	switch (res) {
	case 0:
		return res;
	case EINVAL: /* Invalid arg for pthread_setcancelstate's first param */
		panic(1);
	default:
		panic(1);
	}
}

int fiber_thread_create(tid *thread_id, fiber_job_function_t runner, void *arg)
{
	int res;

	fiber_assert(thread_id != NULL);
	fiber_assert(runner != NULL);
	res = pthread_create(thread_id, NULL, runner, arg);

	/* Invalid attr settings */
	fiber_assert(res != EINVAL);
	/* Nor permitted to set scheduling policy in attr */
	fiber_assert(res != EPERM);

	switch (res) {
	case 0:
		return res;
	/* Cannot create another thread due to
         * 1. Insufficient system resources.
         * 2. A system imposed limit on the number of threads/processes a
         *    user can have.
         */
	case EAGAIN:
		return FBR_ETHRD_LIMIT;
	default:
		panic(1);
	}
}

void fiber_thread_exit(void *ret_val)
{
	pthread_exit(ret_val);
}

int fiber_thread_detach(const tid *thread_id)
{
	int res;

	fiber_assert(thread_id != NULL);
	res = pthread_detach(*thread_id);

	switch (res) {
	case 0:
		return res;
	/* Thread is not joinable. This is ok because that is what we wanted */
	case EINVAL:
		return 0;
	case ESRCH: /* No thread with *thread_id found */
		panic(1);
	default:
		panic(1);
	}
}

int fiber_thread_join(const tid *thread_id, void **ret_val)
{
	int res;

	fiber_assert(thread_id != NULL);
	res = pthread_join(*thread_id, ret_val);

	switch (res) {
	case 0:
		return res;
	/* Thread is not joinable (was detached) or another thread is joining the thread. */
	case EINVAL:
		panic(1);
	case EDEADLK: /* Two threads were trying to join each other */
		panic(1);
	case ESRCH: /* No thread with the id was found */
		panic(1);
	default:
		panic(1);
	}
}

int fiber_thread_cancel_enable(void)
{
	return __fiber_thread_setcancelstate(PTHREAD_CANCEL_ENABLE);
}

int fiber_thread_cancel_disable(void)
{
	return __fiber_thread_setcancelstate(PTHREAD_CANCEL_DISABLE);
}

int fiber_thread_cancel_type_set(int cancel_type)
{
	int res;

	fiber_assert(cancel_type == PTHREAD_CANCEL_DEFERRED ||
		     cancel_type == PTHREAD_CANCEL_ASYNCHRONOUS);
	res = pthread_setcanceltype(cancel_type, NULL);

	switch (res) {
	case 0:
		return res;
	case EINVAL: /* Invalid arg for pthread_setcanceltype's first param */
		panic(1);
	default:
		panic(1);
	}
}

int fiber_thread_cancel(const tid *thread_id)
{
	int res;

	fiber_assert(thread_id != NULL);
	res = pthread_cancel(*thread_id);

	switch (res) {
	case 0:
		return res;
	case ESRCH:
		panic(1);
	default:
		panic(1);
	}
}

#if defined(FIBER_BUILD_ENV_TEST)
#include "test_internal.h"
struct fiber_test_internal_threading_pthread
	fiber_test_internal_threading_pthread = {
		__fiber_thread_setcancelstate
	};
#endif /* FIBER_BUILD_ENV_TEST */

#if defined(FIBER_BUILD_ENV_TEST)
const struct fiber_threading_vtable threading_vtable = {
	fiber_sem_init,
	fiber_sem_destroy,
	fiber_sem_wait,
	fiber_sem_trywait,
	fiber_sem_post,
	fiber_sem_getvalue,
	fiber_mutex_init,
	fiber_mutex_destroy,
	fiber_mutex_lock,
	fiber_mutex_unlock,
	fiber_thread_create,
	fiber_thread_exit,
	fiber_thread_detach,
	fiber_thread_join,
	fiber_thread_cancel_enable,
	fiber_thread_cancel_disable,
	fiber_thread_cancel_type_set,
	fiber_thread_cancel,
};
#endif /* FIBER_BUILD_ENV_TEST */
#if defined(FIBER_THREADING_INTERCEPT) && defined(FIBER_BUILD_ENV_TEST)
#undef fiber_sem_init
#undef fiber_sem_destroy
#undef fiber_sem_wait
#undef fiber_sem_trywait
#undef fiber_sem_post
#undef fiber_sem_getvalue
#undef fiber_mutex_init
#undef fiber_mutex_destroy
#undef fiber_mutex_lock
#undef fiber_mutex_unlock
#undef fiber_thread_create
#undef fiber_thread_exit
#undef fiber_thread_detach
#undef fiber_thread_join
#undef fiber_thread_cancel_enable
#undef fiber_thread_cancel_disable
#undef fiber_thread_cancel_type_set
#undef fiber_thread_cancel
#endif /* FIBER_THREADING_INTERCEPT && FIBER_BUILD_ENV_TEST */
