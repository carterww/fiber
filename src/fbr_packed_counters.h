/* See LICENSE file for copyright and license details. */

#ifndef _FBR_PACKED_COUNTERS_H
#define _FBR_PACKED_COUNTERS_H

#include <stdint.h>

#include <ck_pr.h>

#include "fbr_platform.h"

#if defined(FBR_ARCH_64_BIT)
struct fbr_packed_counters {
	uint32_t lo;
	uint32_t hi;
};

union fbr_packed_counters_union {
	struct fbr_packed_counters parts;
	uint64_t raw;
};

inline static struct fbr_packed_counters
fbr_packed_counters_load(const union fbr_packed_counters_union *packed)
{
	uint64_t raw;
	union fbr_packed_counters_union counters;

	raw = ck_pr_load_64(&packed->raw);

	counters.raw = raw;
	return counters.parts;
}

#define FIBER_PACKED_COUNTERS_OP(lo_op_name, LO_OP, hi_op_name, HI_OP)   \
	inline static void                                               \
		fiber_packed_counters_lo##lo_op_name##_hi##hi_op_name(   \
			union fbr_packed_counters_union *packed,         \
			uint32_t delta_lo, uint32_t delta_hi)            \
	{                                                                \
		union fbr_packed_counters_union set;                     \
		uint64_t old;                                            \
		old = ck_pr_load_64(&packed->raw);                       \
		do {                                                     \
			set.raw = old;                                   \
			set.parts.lo LO_OP delta_lo;                     \
			set.parts.hi HI_OP delta_hi;                     \
		} while (!ck_pr_cas_64_value(&packed->raw, old, set.raw, \
					     &old));                     \
	}

FIBER_PACKED_COUNTERS_OP(add, +=, add, +=)
FIBER_PACKED_COUNTERS_OP(add, +=, sub, -=)
FIBER_PACKED_COUNTERS_OP(sub, -=, add, +=)
FIBER_PACKED_COUNTERS_OP(sub, -=, sub, -=)
#undef FIBER_PACKED_COUNTERS_OP

#define FIBER_PACKED_COUNTERS_SINGLE_OP(op_name, OP, counter_name)           \
	inline static void fiber_packed_counters_##counter_name##_##op_name( \
		union fbr_packed_counters_union *packed,                     \
		uint32_t delta_##counter_name)                               \
	{                                                                    \
		union fbr_packed_counters_union set;                         \
		uint64_t old;                                                \
		old = ck_pr_load_64(&packed->raw);                           \
		do {                                                         \
			set.raw = old;                                       \
			set.parts.counter_name OP delta_##counter_name;      \
		} while (!ck_pr_cas_64_value(&packed->raw, old, set.raw,     \
					     &old));                         \
	}

FIBER_PACKED_COUNTERS_SINGLE_OP(add, +=, lo)
FIBER_PACKED_COUNTERS_SINGLE_OP(sub, -=, lo)
FIBER_PACKED_COUNTERS_SINGLE_OP(add, +=, hi)
FIBER_PACKED_COUNTERS_SINGLE_OP(sub, -=, hi)
#undef FIBER_PACKED_COUNTERS_SINGLE_OP

#elif defined(FBR_ARCH_32_BIT)
#error Not supported on 32 bit architectures yet.
#endif
#undef TWQL_CMP_XCHNG
#endif /* _FBR_PACKED_COUNTERS_H */
