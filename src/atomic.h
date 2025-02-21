/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_ATOMIC_H
#define _FIBER_ATOMIC_H

#include "fiber.h"

#if defined(FIBER_ATOMIC_OPERATIONS_IMPL_GCC) || \
	defined(FIBER_ATOMIC_OPERATIONS_IMPL_CLANG)
enum fiber_atomic_memorder {
	FIBER_ATOMIC_RELAXED = __ATOMIC_RELAXED,
	FIBER_ATOMIC_ACQUIRE = __ATOMIC_ACQUIRE,
	FIBER_ATOMIC_RELEASE = __ATOMIC_RELEASE,
	FIBER_ATOMIC_ACQ_REL = __ATOMIC_ACQ_REL,
	FIBER_ATOMIC_SEQ_CST = __ATOMIC_SEQ_CST
};
#else
#error "ATOMIC_OPERATIONS_IMPL was not set to a valid value in config.mk"
#endif /* FIBER_ATOMIC_OPERATIONS_IMPL_GCC or FIBER_ATOMIC_OPERATIONS_IMPL_CLANG */

/* The fetch_add and fetch_sub function return the previous value in at the pointer.
 * The add_fetch and sub_fetch return the result of the operation. The atomic 'and' and
 * 'or' operations follow the same pattern.
 */

/** Thread pool size atomic ops **/

tpsize atomic_fetch_sub_tpsize(tpsize *t, int val,
			       enum fiber_atomic_memorder memorder);
tpsize atomic_fetch_add_tpsize(tpsize *t, int val,
			       enum fiber_atomic_memorder memorder);

tpsize atomic_sub_fetch_tpsize(tpsize *t, int val,
			       enum fiber_atomic_memorder memorder);
tpsize atomic_add_fetch_tpsize(tpsize *t, int val,
			       enum fiber_atomic_memorder memorder);

tpsize atomic_load_tpsize(tpsize *t, enum fiber_atomic_memorder memorder);

/** Job Id atomic ops **/

jid atomic_add_fetch_jid(jid *j, int val, enum fiber_atomic_memorder memorder);

void atomic_store_jid(jid *j, jid val, enum fiber_atomic_memorder memorder);

jid atomic_load_jid(jid *j, enum fiber_atomic_memorder memorder);

/* This operation should compare the contents of j with the contents of expected.
 * If they are equal, it should perform an read-modify-write on j with the value
 * of expected and return a non zero value. If they are not equal (or the cmp exchange
 * failed for some other reason), the value of j should be written to expected, and
 * the function should return 0.
 * @param j -> Pointer to the job id to update.
 * @param expected -> Pointer to the value that is expected to be in j.
 * @param new -> The value that should be written to j if *expected = *j.
 * @param weak -> 1 if a weak compare exchange should be performed. 0 if a strong
 * compare exchange should be performed. Weak cmp exchanges can fail spuriously so only
 * use this if you are doing the cmp exchange in a retry loop.
 * @param success_memorder -> The memory order to use if the operation succeeds.
 * @param failure_memorder -> The memory order to use if the operation fails. Specifically,
 * the memory order of loading *j into expected on failure.
 * @returns -> A non zero value if the operation succeeds. 0 if the operation fails.
 */
int atomic_compare_exchange_jid(jid *j, jid *expected, jid new, int weak,
				enum fiber_atomic_memorder success_memorder,
				enum fiber_atomic_memorder failure_memorder);

/** uint32_t atomic ops **/

uint32_t atomic_load_uint32(uint32_t *u, enum fiber_atomic_memorder memorder);

uint32_t atomic_and_fetch_uint32(uint32_t *u, uint32_t val,
				 enum fiber_atomic_memorder memorder);

uint32_t atomic_or_fetch_uint32(uint32_t *u, uint32_t val,
				enum fiber_atomic_memorder memorder);

#endif /* _FIBER_ATOMIC_H */
