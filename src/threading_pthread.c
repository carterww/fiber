/* See LICENSE file for copyright and license details. */

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stddef.h>

#include "fiber/fiber.h"
#include "threading.h"
#include "utils.h"

/* This is here to support the threading_fault module in test/mock/threading.
 * Redefining these functions and putting them into a vtable allows us to use
 * their implementation while adding some other code before and after it. Don't
 * worry about this at all, it is just for testing purposes.
 */
#if defined(FIBER_THREADING_INTERCEPT) && defined(FIBER_BUILD_ENV_TEST)
#define fiber_thread_create _fiber_thread_create
#define fiber_thread_exit _fiber_thread_exit
#define fiber_thread_detach _fiber_thread_detach
#define fiber_thread_join _fiber_thread_join
#define fiber_thread_cancel_enable _fiber_thread_enable
#define fiber_thread_cancel_disable _fiber_thread_disable
#define fiber_thread_cancel_type_set _fiber_thread_cancel_type_set
#define fiber_thread_cancel _fiber_thread_cancel
#endif /* FIBER_TEST_THREADING_MOCK && FIBER_BUILD_ENV_TEST */

static int _fiber_thread_setcancelstate(int state)
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
	return _fiber_thread_setcancelstate(PTHREAD_CANCEL_ENABLE);
}

int fiber_thread_cancel_disable(void)
{
	return _fiber_thread_setcancelstate(PTHREAD_CANCEL_DISABLE);
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
	fiber_thread_create,	      fiber_thread_exit,
	fiber_thread_detach,	      fiber_thread_join,
	fiber_thread_cancel_enable,   fiber_thread_cancel_disable,
	fiber_thread_cancel_type_set, fiber_thread_cancel,
};
#endif /* FIBER_BUILD_ENV_TEST */
#if defined(FIBER_THREADING_INTERCEPT) && defined(FIBER_BUILD_ENV_TEST)
#undef fiber_thread_create
#undef fiber_thread_exit
#undef fiber_thread_detach
#undef fiber_thread_join
#undef fiber_thread_cancel_enable
#undef fiber_thread_cancel_disable
#undef fiber_thread_cancel_type_set
#undef fiber_thread_cancel
#endif /* FIBER_THREADING_INTERCEPT && FIBER_BUILD_ENV_TEST */
