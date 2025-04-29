/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_WAIT_H
#define _FIBER_WAIT_H

#include "fiber/fiber.h"

enum fiber_wait_condition {
	FIBER_WAIT_CONDITION_FALSE = 0,
	FIBER_WAIT_CONDITION_THREADS_NUMBER_0 = 1,
	FIBER_WAIT_CONDITION_TW_AND_QL_0 = 2
};

struct fiber_wait_list_node {
        unsigned long epoch;
        struct fiber_wait_list_node *next;
        int futex;
};

struct fiber_wait_retired_node {
	struct fiber_wait_list_node *node;
	unsigned long epoch; /* When node was retired */
};

struct fiber_wait_list {
        unsigned long epoch_global;
        struct fiber_wait_list_node *active;
        struct fiber_wait_list_node *inactive;
};

enum fiber_wait_condition
fiber_wait_check_condition(const struct fiber_pool *pool);

#define fiber_wait_can_sleep(pool) \
	(FIBER_WAIT_CONDITION_FALSE == fiber_wait_check_condition(pool))

#define fiber_wait_can_wake(pool) \
	(FIBER_WAIT_CONDITION_FALSE != fiber_wait_check_condition(pool))

#define fiber_wait_will_be_woken(pool) \
	(FIBER_WAIT_CONDITION_FALSE == fiber_wait_check_condition(pool))

void fiber_wait_wake_waiters(struct fiber_pool *pool);

void fiber_wait_wait_on_waker(struct fiber_pool *pool);

#endif /* _FIBER_WAIT_H */
