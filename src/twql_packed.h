/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_TWORKING_QLENGTH_PACKED_H
#define _FIBER_TWORKING_QLENGTH_PACKED_H

#include "fiber/fiber.h"
#include "fiber_atomic/atomic.h"
#include "utils.h"

struct fiber_twql {
	tpsize threads_working;
	qsize queue_length;
};

/* Pack threads_working and queue_length into a single word so
 * we can atomically load both variables. This is important with respect
 * to fiber_wait (see docs/fiber_wait.md). You may need to adjust the types
 * of this struct in the following cases:
 * 1. fiber_twql_packed_raw cannot fit the two counters.
 * 2. fiber_twql_packed_raw cannot be atomically loaded because it is too large.
 * 3. You modify the types of qsize and tpsize.
 * 4. sizeof(qsize) + sizeof(tpsize) > sizeof(void *). In this case we
 *    cannot atomically load the packed variable. The static_assert
 *    below ensures the code will not compile if this is the case.
 */
typedef long fiber_twql_packed_raw;
union fiber_twql_packed {
	fiber_twql_packed_raw packed;
	struct fiber_twql counters;
};
static_assert(sizeof(fiber_twql_packed_raw) >= sizeof(struct fiber_twql),
	      tw_ql_packed_must_be_gte_counters_struct);
static_assert(sizeof(union fiber_twql_packed) <= sizeof(void *),
	      twql_union_must_be_lte_pointer_size);

static struct fiber_twql fiber_twql_load(const union fiber_twql_packed *packed,
					 enum fiber_atomic_memorder memorder)
{
	union fiber_twql_packed u;
	fiber_twql_packed_raw raw;

	raw = fiber_atomic_load(&packed->packed, memorder);
	u.packed = raw;

	return u.counters;
}

#define TWQL_CMP_XCHNG(packed_ptr, loop_block)                        \
	do {                                                          \
		union fiber_twql_packed u;                            \
		fiber_twql_packed_raw old;                            \
		old = fiber_atomic_load(&packed->packed,              \
					FIBER_ATOMIC_ACQUIRE);        \
		do {                                                  \
			u.packed = old;                               \
			loop_block                                    \
		} while (!fiber_atomic_cmp_xchng(                     \
			&packed->packed, &old, u.packed, 1,           \
			FIBER_ATOMIC_ACQ_REL, FIBER_ATOMIC_ACQUIRE)); \
	} while (0)

static void fiber_twql_add(union fiber_twql_packed *packed, tpsize delta_tw,
			   qsize delta_ql)
{
	TWQL_CMP_XCHNG(packed, {
		u.counters.threads_working += delta_tw;
		u.counters.queue_length += delta_ql;
	});
}

static void fiber_twql_queue_length_add(union fiber_twql_packed *packed,
					qsize delta_ql)
{
	TWQL_CMP_XCHNG(packed, { u.counters.queue_length += delta_ql; });
}

static void fiber_twql_threads_working_add(union fiber_twql_packed *packed,
					   tpsize delta_tw)
{
	TWQL_CMP_XCHNG(packed, { u.counters.threads_working += delta_tw; });
}

#undef TWQL_CMP_XCHNG
#endif /* _FIBER_TWORKING_QLENGTH_PACKED_H */
