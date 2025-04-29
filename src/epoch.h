/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_EPOCH_H
#define _FIBER_EPOCH_H

#include "fiber_atomic/atomic.h"

/* Loads the current epoch pointed to be epoch_ptr and returns its value */
#define fiber_epoch_load(epoch_ptr) \
	(fiber_atomic_load(epoch_ptr, FIBER_ATOMIC_ACQUIRE));

/* Advances to the next epoch and returns its new value */
#define fiber_epoch_advance(epoch_ptr) \
	(fiber_atomic_inc_fetch(epoch_ptr, FIBER_ATOMIC_ACQ_REL));

/* Compares two epochs, a and b, and returns
 * 1. 0 if they are equal.
 * 2. A negative number if a is before b.
 * 3. A positive number if a is after b.
 */
static long fiber_epoch_cmp(unsigned long a, unsigned long b)
{
        /* By subracting two unsigned longs and casting the result to a signed
         * long we get a negative number if the upper bit is set (a - b > 
         * ULONG_MAX / 2). In this case a is before b.
         *
         * This assumes negative numbers are represented with the upper bit
         * set (all modern hardware?).
         */
	return (long)(a - b);
}

#endif /* _FIBER_EPOCH_H */
