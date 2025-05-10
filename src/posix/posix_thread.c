/* See LICENSE file for copyright and license details. */

#include <errno.h>
#include <pthread.h>
#include <stddef.h>

#include <fbr_errno.h>

#include "fbr_debug.h"
#include "fbr_thread.h"

/* This is here to support the threading_fault module in test/mock/threading.
 * Redefining these functions and putting them into a vtable allows us to use
 * their implementation while adding some other code before and after it. Don't
 * worry about this at all, it is just for testing purposes.
 */
#if defined(FIBER_THREADING_INTERCEPT)
#define fbr_thread_create _fbr_thread_create
#define fbr_thread_exit _fbr_thread_exit
#define fbr_thread_detach _fbr_thread_detach
#define fbr_thread_join _fbr_thread_join
#define fbr_thread_cancel_enable _fbr_thread_cancel_enable
#define fbr_thread_cancel_disable _fbr_thread_cancel_disable
#define fbr_thread_cancel_type_set _fbr_thread_cancel_type_set
#define fbr_thread_cancel _fbr_thread_cancel
#endif /* FIBER_TEST_THREADING_MOCK */

static fbr_errno_t _fbr_thread_setcancelstate(int state)
{
	int res;

	fbr_assert(state == PTHREAD_CANCEL_ENABLE ||
		   state == PTHREAD_CANCEL_DISABLE);
	res = pthread_setcancelstate(state, NULL);

	if (res == 0) {
		return FBR_EOK;
	}
	/* Invalid arg for pthread_setcancelstate's first param */
	fbr_assert(res != EINVAL);

	switch (res) {
	default:
		fbr_panic(res);
		return FBR_EGENERIC;
	}
}

fbr_errno_t fbr_thread_create(tid_t *thread_id, void *(*entry)(void *),
			      void *arg)
{
	int res;

	fbr_assert(thread_id != NULL);
	fbr_assert(entry != NULL);
	res = pthread_create(thread_id, NULL, entry, arg);

	if (res == 0) {
		return FBR_EOK;
	}

	/* Invalid attr settings */
	fbr_assert(res != EINVAL);
	/* Nor permitted to set scheduling policy in attr */
	fbr_assert(res != EPERM);

	switch (res) {
	/* Cannot create another thread due to
         * 1. Insufficient system resources.
         * 2. A system imposed limit on the number of threads/processes a
         *    user can have.
         */
	case EAGAIN:
		return FBR_ETHRD_LIMIT;
	default:
		fbr_panic(res);
		return FBR_EGENERIC;
	}
}

void fbr_thread_exit(void *ret_val)
{
	pthread_exit(ret_val);
}

fbr_errno_t fbr_thread_detach(const tid_t *thread_id)
{
	int res;

	fbr_assert(thread_id != NULL);
	res = pthread_detach(*thread_id);

	if (res == 0) {
		return FBR_EOK;
	}

	/* No thread with *thread_id found */
	fbr_assert(res != ESRCH);

	switch (res) {
	/* Thread is not joinable. This is ok because that is what we wanted */
	case EINVAL:
		return FBR_EOK;
	default:
		fbr_panic(res);
		return FBR_EGENERIC;
	}
}

fbr_errno_t fbr_thread_join(const tid_t *thread_id, void **ret_val)
{
	int res;

	fbr_assert(thread_id != NULL);
	res = pthread_join(*thread_id, ret_val);

	if (res == 0) {
		return FBR_EOK;
	}

	/* Thread is not joinable (was detached) or another thread is joining the thread. */
	fbr_assert(res != EINVAL);
	/* Two threads were trying to join each other */
	fbr_assert(res != EDEADLK);
	/* No thread with the id was found */
	fbr_assert(res != ESRCH);

	switch (res) {
	default:
		fbr_panic(res);
		return FBR_EGENERIC;
	}
}

fbr_errno_t fbr_thread_cancel_enable(void)
{
	return _fbr_thread_setcancelstate(PTHREAD_CANCEL_ENABLE);
}

fbr_errno_t fbr_thread_cancel_disable(void)
{
	return _fbr_thread_setcancelstate(PTHREAD_CANCEL_DISABLE);
}

fbr_errno_t fbr_thread_cancel_type_set(int cancel_type)
{
	int res;

	fbr_assert(cancel_type == PTHREAD_CANCEL_DEFERRED ||
		   cancel_type == PTHREAD_CANCEL_ASYNCHRONOUS);
	res = pthread_setcanceltype(cancel_type, NULL);

	if (res == 0) {
		return FBR_EOK;
	}

	/* Invalid arg for pthread_setcanceltype's first param */
	fbr_assert(res != EINVAL);

	switch (res) {
	default:
		fbr_panic(res);
		return FBR_EGENERIC;
	}
}

fbr_errno_t fbr_thread_cancel(const tid_t *thread_id)
{
	int res;

	fbr_assert(thread_id != NULL);
	res = pthread_cancel(*thread_id);

	if (res == 0) {
		return FBR_EOK;
	}

	fbr_assert(res != ESRCH);

	switch (res) {
	default:
		fbr_panic(res);
		return FBR_EGENERIC;
	}
}

#if defined(FIBER_BUILD_ENV_TEST)
#include "test_internal.h"
struct fbr_test_internal_threading_pthread
	fbr_test_internal_threading_pthread = { _fbr_thread_setcancelstate };
#endif /* FIBER_BUILD_ENV_TEST */

#if defined(FIBER_THREADING_INTERCEPT)
#undef fbr_thread_create
#undef fbr_thread_exit
#undef fbr_thread_detach
#undef fbr_thread_join
#undef fbr_thread_cancel_enable
#undef fbr_thread_cancel_disable
#undef fbr_thread_cancel_type_set
#undef fbr_thread_cancel

fbr_errno_t (*fbr_thread_create_fn_ptr)(tid_t *, fbr_job_function_t,
					void *) = _fbr_thread_create;
void (*fbr_thread_exit_fn_ptr)(void *) = _fbr_thread_exit;
fbr_errno_t (*fbr_thread_detach_fn_ptr)(const tid_t *) = _fbr_thread_detach;
fbr_errno_t (*fbr_thread_join_fn_ptr)(const tid_t *,
				      void **) = _fbr_thread_join;
fbr_errno_t (*fbr_thread_cancel_enable_fn_ptr)(void) = _fbr_thread_cancel_enable;
fbr_errno_t (*fbr_thread_cancel_disable_fn_ptr)(void) =
	_fbr_thread_cancel_disable;
fbr_errno_t (*fbr_thread_cancel_type_set_fn_ptr)(int) =
	_fbr_thread_cancel_type_set;
fbr_errno_t (*fbr_thread_cancel_fn_ptr)(const tid_t *) = _fbr_thread_cancel;
#endif /* FIBER_THREADING_INTERCEPT */
