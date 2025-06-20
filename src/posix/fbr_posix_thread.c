// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Carter Williams

#include <errno.h>
#include <pthread.h>
#include <stddef.h>

#include <fbr_errno.h>

#include "fbr_debug.h"
#include "fbr_thread.h"

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
			      void *arg, size_t stack_size)
{
	int res;
	pthread_attr_t attr;

	fbr_assert(thread_id != NULL);
	fbr_assert(entry != NULL);

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (stack_size != 0) {
		pthread_attr_setstacksize(&attr, stack_size);
	}
	res = pthread_create(thread_id, &attr, entry, arg);

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

fbr_errno_t fbr_thread_cancel_enable(void)
{
	return _fbr_thread_setcancelstate(PTHREAD_CANCEL_ENABLE);
}

fbr_errno_t fbr_thread_cancel_disable(void)
{
	return _fbr_thread_setcancelstate(PTHREAD_CANCEL_DISABLE);
}
