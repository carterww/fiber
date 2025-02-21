/* See LICENSE file for copyright and license details. */

#include "atomic.h"

#if !defined(__GNUC__) && !defined(__clang__)
#warning \
	"This file uses compiler extensions that may be be supported by your compiler."
#endif

/** Thread pool size atomic ops **/

tpsize atomic_fetch_sub_tpsize(tpsize *t, int val,
			       enum fiber_atomic_memorder memorder)
{
	return __atomic_fetch_sub(t, val, memorder);
}

tpsize atomic_fetch_add_tpsize(tpsize *t, int val,
			       enum fiber_atomic_memorder memorder)
{
	return __atomic_fetch_add(t, val, memorder);
}

tpsize atomic_sub_fetch_tpsize(tpsize *t, int val,
			       enum fiber_atomic_memorder memorder)
{
	return __atomic_sub_fetch(t, val, memorder);
}

tpsize atomic_add_fetch_tpsize(tpsize *t, int val,
			       enum fiber_atomic_memorder memorder)
{
	return __atomic_add_fetch(t, val, memorder);
}

tpsize atomic_load_tpsize(tpsize *t, enum fiber_atomic_memorder memorder)
{
	return __atomic_load_n(t, memorder);
}

/** Job Id atomic ops **/

jid atomic_add_fetch_jid(jid *j, int val, enum fiber_atomic_memorder memorder)
{
	return __atomic_add_fetch(j, val, memorder);
}

void atomic_store_jid(jid *j, jid val, enum fiber_atomic_memorder memorder)
{
	__atomic_store_n(j, val, memorder);
}

jid atomic_load_jid(jid *j, enum fiber_atomic_memorder memorder)
{
	return __atomic_load_n(j, memorder);
}

int atomic_compare_exchange_jid(jid *j, jid *expected, jid new, int weak,
				enum fiber_atomic_memorder success_memorder,
				enum fiber_atomic_memorder failure_memorder)
{
	return __atomic_compare_exchange_n(j, expected, new, weak,
					   success_memorder, failure_memorder);
}

/** uint32_t atomic ops **/

uint32_t atomic_load_uint32(uint32_t *u, enum fiber_atomic_memorder memorder)
{
	return __atomic_load_n(u, memorder);
}

uint32_t atomic_and_fetch_uint32(uint32_t *u, uint32_t val,
				 enum fiber_atomic_memorder memorder)
{
	return __atomic_and_fetch(u, val, memorder);
}

uint32_t atomic_or_fetch_uint32(uint32_t *u, uint32_t val,
				enum fiber_atomic_memorder memorder)
{
	return __atomic_or_fetch(u, val, memorder);
}
