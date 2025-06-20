// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Carter Williams

#define _GNU_SOURCE

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include <linux/futex.h>
#include <sys/syscall.h>

#include <ck_pr.h>

#include <fbr_errno.h>

#include "fbr_debug.h"
#include "fbr_futex.h"

inline static long futex_syscall_linux_timeout(uint32_t *futex1,
					       uint32_t *futex2, int futex_op,
					       uint32_t value1,
					       const struct timespec *timeout,
					       uint32_t value3)
{
	return syscall((long)SYS_futex, futex1, futex_op, value1, timeout,
		       futex2, value3);
}

inline static long futex_syscall_linux_value2(uint32_t *futex1,
					      uint32_t *futex2, int futex_op,
					      uint32_t value1, uint32_t value2,
					      uint32_t value3)
{
	uintptr_t value2_uptr = (uintptr_t)value2;
	void *value2_as_ptr = (void *)value2_uptr;
	return syscall((long)SYS_futex, futex1, futex_op, value1, value2_as_ptr,
		       futex2, value3);
}

fbr_errno_t fbr_futex_wait(uint32_t *futex, uint32_t expected)
{
	long syscall_res = -1;
	uint32_t value;

	while (true) {
		value = ck_pr_load_32(futex);
		if (value != expected) {
			break;
		}
		ck_pr_barrier();
		syscall_res = futex_syscall_linux_timeout(
			futex, NULL, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, expected,
			NULL, 0);
		if (syscall_res == 0) {
			return FBR_EOK;
		} else if (syscall_res == -1 && errno != EINTR) {
			break;
		}
	}
	if (value != expected || errno == EAGAIN) {
		return FBR_EAGAIN;
	}

	fbr_assert(errno != EACCES);
	fbr_assert(errno != EFAULT);
	fbr_assert(errno != EINVAL);
	fbr_assert(errno != ETIMEDOUT);
	fbr_panic(errno);
	return FBR_EGENERIC;
}

fbr_errno_t fbr_futex_wait_timeout(uint32_t *futex, uint32_t expected,
				   unsigned long *timeout_ms)
{
	long syscall_res = -1;
	uint32_t value;
	struct timespec timeout;
	time_t time_ms;
	long waited_ms;

	time_ms = (time_t)*timeout_ms;
	timeout.tv_sec = time_ms / 1000;
	timeout.tv_nsec = (time_ms % 1000) * (time_t)1000000;

	waited_ms = 0;
	while (true) {
		struct timespec start, end;
		time_t sec, ns;

		value = ck_pr_load_32(futex);
		if (value != expected) {
			break;
		}
		/* 400 cycles on a 2GHz processor is about 200 ns. The clock^ and
                 * futex syscall will likely be at least 400 cycles so we shoud just
                 * timeout.
                 * ^ clock_gettime does not always issue a syscall.
                 */
		if (timeout.tv_sec <= 0 && timeout.tv_nsec <= 200) {
			return FBR_ETIMEDOUT;
		}

		clock_gettime(CLOCK_MONOTONIC, &start);
		syscall_res = futex_syscall_linux_timeout(
			futex, NULL, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, expected,
			&timeout, 0);
		clock_gettime(CLOCK_MONOTONIC, &end);
		sec = end.tv_sec - start.tv_sec;
		ns = end.tv_nsec - start.tv_nsec;
		timeout.tv_sec -= sec;
		timeout.tv_nsec -= ns;
		if (ns < 0) {
			sec -= 1;
			ns += 1000000000;
		}
		if (timeout.tv_nsec < 0) {
			timeout.tv_sec -= 1;
			timeout.tv_nsec += 1000000000;
		}
		waited_ms += sec * 1000;
		waited_ms += ns / 1000000;
		if (syscall_res == 0) {
			break;
		} else if (syscall_res == -1 && errno != EINTR) {
			break;
		}
	}
	fbr_assert(waited_ms >= 0);
	*timeout_ms = (unsigned long)waited_ms;
	if (syscall_res == 0) {
		return FBR_EOK;
	}
	if (value != expected || errno == EAGAIN) {
		return FBR_EAGAIN;
	} else if (errno == ETIMEDOUT) {
		return FBR_ETIMEDOUT;
	}

	fbr_assert(errno != EACCES);
	fbr_assert(errno != EFAULT);
	fbr_assert(errno != EINVAL);
	fbr_panic(errno);
	return FBR_EGENERIC;
}

fbr_errno_t fbr_futex_wake(uint32_t *futex, uint32_t *num_threads)
{
	long num_woken;

	if (*num_threads == 0) {
		return FBR_EINVAL;
	}
	if (*num_threads > INT_MAX) {
		*num_threads = INT_MAX;
	}

	num_woken = futex_syscall_linux_timeout(futex, NULL,
						FUTEX_WAKE | FUTEX_PRIVATE_FLAG,
						*num_threads, NULL, 0);

	fbr_assert(num_woken >= 0 && num_woken <= INT_MAX);

	*num_threads = (uint32_t)num_woken;
	return FBR_EOK;
}
