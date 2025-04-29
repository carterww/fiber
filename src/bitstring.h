/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_BITSTRING_H
#define _FIBER_BITSTRING_H

#include <stddef.h>

#include "debug.h"
#include "fiber_atomic/atomic.h"
#include "platform.h"

typedef unsigned int fiber_bitstring_word;

struct fiber_bitstring {
	size_t word_num;
	fiber_bitstring_word *words;
};

#define LOG2_BYTES_TO_BITS(x) \
	(((x) == 1) ? 3 : ((x) == 2) ? 4 : ((x) == 4) ? 5 : ((x) == 8) ? 6 : -1)

#define FIBER_BITSTRING_WORD_NUM(bit_num)          \
	((size_t)(bit_num +                        \
		  (FIBER_PLATFORM_BITS_PER_BYTE *  \
		   sizeof(fiber_bitstring_word)) - \
		  1) >>                            \
	 LOG2_BYTES_TO_BITS(sizeof(fiber_bitstring_word)))

#define FIBER_BITSTRING_BYTES(bit_num) \
	(FIBER_BITSTRING_WORD_NUM(bit_num) * sizeof(fiber_bitstring_word))

#define FIBER_BITSTRING_BITS_PER_WORD \
	(sizeof(fiber_bitstring_word) * FIBER_PLATFORM_BITS_PER_BYTE)

#define FIBER_BITSTRING_IDX_SHIFT(bs_ptr, bit_num, idx, shift)   \
	do {                                                     \
		idx = bit_num / FIBER_BITSTRING_BITS_PER_WORD;   \
		shift = bit_num % FIBER_BITSTRING_BITS_PER_WORD; \
		fiber_assert(idx < bs->word_num);                \
	} while (0)

#define FIBER_BITSTRING_BIT(value, shift)          \
	(((fiber_bitstring_word)((value) ? 1 : 0)) \
	 << shift % FIBER_BITSTRING_BITS_PER_WORD)

static void fiber_bitstring_init(struct fiber_bitstring *bs,
				 fiber_bitstring_word initial)
{
	size_t i;

	fiber_assert(bs != NULL);

	for (i = 0; i < bs->word_num; ++i) {
		bs->words[i] = initial;
	}
}

static fiber_bitstring_word
fiber_bitstring_get(const struct fiber_bitstring *bs, size_t bit_num)
{
	size_t idx;
	size_t shift;

	fiber_assert(bs != NULL);
	FIBER_BITSTRING_IDX_SHIFT(bs, bit_num, idx, shift);

	return bs->words[idx] & ((fiber_bitstring_word)1 << shift);
}

static fiber_bitstring_word
fiber_bitstring_get_atomic(const struct fiber_bitstring *bs, size_t bit_num,
			   enum fiber_atomic_memorder memorder)
{
	size_t idx;
	size_t shift;
	fiber_bitstring_word word;

	fiber_assert(bs != NULL);
	FIBER_BITSTRING_IDX_SHIFT(bs, bit_num, idx, shift);

	word = fiber_atomic_load(&bs->words[idx], memorder);

	return word & ((fiber_bitstring_word)1 << shift);
}

static void fiber_bitstring_set(struct fiber_bitstring *bs, size_t bit_num,
				int value)
{
	size_t idx;
	size_t shift;
	fiber_bitstring_word val;

	fiber_assert(bs != NULL);
	FIBER_BITSTRING_IDX_SHIFT(bs, bit_num, idx, shift);

	val = (fiber_bitstring_word)1 << shift;
	if (value) {
		bs->words[idx] |= val;
	} else {
		bs->words[idx] &= ~val;
	}
}

static void
fiber_bitstring_set_atomic(struct fiber_bitstring *bs, size_t bit_num,
			   int value,
			   enum fiber_atomic_memorder success_memorder,
			   enum fiber_atomic_memorder failure_memorder)
{
#define CAS_LOOP(body)                                                         \
	do {                                                                   \
		body                                                           \
	} while (!fiber_atomic_cmp_xchng(&bs->words[idx], &old_word, new_word, \
					 1, success_memorder,                  \
					 failure_memorder));
	size_t idx;
	size_t shift;
	fiber_bitstring_word shifted_word;
	fiber_bitstring_word old_word;
	fiber_bitstring_word new_word;

	fiber_assert(bs != NULL);
	FIBER_BITSTRING_IDX_SHIFT(bs, bit_num, idx, shift);

	shifted_word = (fiber_bitstring_word)1 << shift;
	old_word = fiber_atomic_load(&bs->words[idx], failure_memorder);
	if (value) {
		CAS_LOOP({ new_word = old_word | shifted_word; })
	} else {
		CAS_LOOP({ new_word = old_word & ~shifted_word; })
	}
#undef CAS_LOOP
}

#endif /* _FIBER_BITSTRING_H */
