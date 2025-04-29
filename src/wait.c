/* See LICENSE file for copyright and license details. */

#include "fiber/fiber.h"
#include "fiber_internal.h"
#include "wait.h"
#include "twql_packed.h"

enum fiber_wait_condition
fiber_wait_check_condition(const struct fiber_pool *pool)
{
	tpsize threads_number;
	struct fiber_twql twql;

	threads_number =
		fiber_atomic_load(&pool->threads_number, FIBER_ATOMIC_ACQUIRE);
	if (threads_number == 0) {
		return FIBER_WAIT_CONDITION_THREADS_NUMBER_0;
	}
	twql = fiber_twql_load(&pool->twql, FIBER_ATOMIC_ACQUIRE);
	if (twql.threads_working == 0 && twql.queue_length == 0) {
		return FIBER_WAIT_CONDITION_TW_AND_QL_0;
	}

	return FIBER_WAIT_CONDITION_FALSE;
}
