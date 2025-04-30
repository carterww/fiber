/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_TLS_H
#define _FIBER_TLS_H

#include "bitstring.h"
#include "list/list.h"
#include "platform.h"
#include "threading.h"
#include "wait.h"

#define FIBER_THREAD_TLS_ENTRY_BOOL_WAIT_EPOCH_ACTIVE (1UL << 0)
#define FIBER_THREAD_TLS_ENTRY_BOOL_ENTRY_USED (1UL << 1)

#define FIBER_THREAD_TLS_ENTRY_WAIT_RETIRED_NODES_CAP (8)

#define FIBER_THREAD_TLS_TABLE_CAP (16)

struct fiber_thread_tls_entry {
	tid thread_id;
	unsigned long bools;
	unsigned long wait_epoch_curr;
	unsigned long wait_retired_nodes_len;
	unsigned long wait_retired_nodes_cap;
	struct fiber_wait_retired_node
		wait_retired_nodes[FIBER_THREAD_TLS_ENTRY_WAIT_RETIRED_NODES_CAP];
};

/* Prevent false sharing between threads by making sure entries are on their
 * own cache lines.
 */
union fiber_thread_tls_entry_padded {
	struct fiber_thread_tls_entry entry;
	unsigned char padding[FIBER_PLATFORM_CACHE_LINE_ALIGNED_BYTES(
		struct fiber_thread_tls_entry)];
};

struct fiber_thread_tls_table {
	struct list_node *list;
	long ref_count;
	unsigned long table_cap;
	union fiber_thread_tls_entry_padded table[FIBER_THREAD_TLS_TABLE_CAP];
	struct fiber_bitstring entry_use_bitstring;
};

/* Don't want other files using these. They should use the cap varialbe in
 * struct in case I decide to malloc the arrays.
 */
#undef FIBER_THREAD_TLS_TABLE_CAP
#undef FIBER_THREAD_TLS_ENTRY_WAIT_RETIRED_NODES_CAP

#endif /* _FIBER_TLS_H */
