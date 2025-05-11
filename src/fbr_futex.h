/* See LICENSE file for copyright and license details. */

#ifndef _FBR_FUTEX_H
#define _FBR_FUTEX_H

#include <stdbool.h>
#include <stdint.h>

#include <fbr_errno.h>

uint32_t fbr_futex_load(uint32_t *futex);

void fbr_futex_set(uint32_t *futex, uint32_t value);

uint32_t fbr_futex_add(uint32_t *futex, uint32_t value);

uint32_t fbr_futex_exchange(uint32_t *futex, uint32_t value);

bool fbr_futex_cas(uint32_t *futex, uint32_t *expected, uint32_t value);

fbr_errno_t fbr_futex_wait(uint32_t *futex, uint32_t expected);

fbr_errno_t fbr_futex_wait_timeout(uint32_t *futex, uint32_t expected,
				   unsigned long *timeout_ms);

fbr_errno_t fbr_futex_wake(uint32_t *futex, uint32_t *num_threads);

#endif /* _FBR_FUTEX_H */
