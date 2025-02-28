#ifndef _FIBER_TEST_MOCK_THREADING_TRACE_FAULT_H
#define _FIBER_TEST_MOCK_THREADING_TRACE_FAULT_H

#include "src/threading.h"

struct threading_trace_fault_fail_after {
	unsigned long count;
	int fail_res;
};

/* threading_trace_fault_*_control structs MUST start with a fiber_mutex
 * and only contain threading_fault_fail_after structs after. I'm lazy so
 * I did pointer arithmetic stuff that relies on this.
 */
struct threading_trace_fault_sem_control {
	fiber_mutex count_lock;
	struct threading_trace_fault_fail_after init;
	struct threading_trace_fault_fail_after destroy;
	struct threading_trace_fault_fail_after wait;
	struct threading_trace_fault_fail_after trywait;
	struct threading_trace_fault_fail_after post;
	struct threading_trace_fault_fail_after getvalue;
};

struct threading_trace_fault_mutex_control {
	fiber_mutex count_lock;
	struct threading_trace_fault_fail_after init;
	struct threading_trace_fault_fail_after destroy;
	struct threading_trace_fault_fail_after lock;
	struct threading_trace_fault_fail_after unlock;
};

struct threading_trace_fault_thread_control {
	fiber_mutex count_lock;
	struct threading_trace_fault_fail_after create;
	struct threading_trace_fault_fail_after detach;
	struct threading_trace_fault_fail_after join;
	struct threading_trace_fault_fail_after cancel_enable;
	struct threading_trace_fault_fail_after cancel_disable;
	struct threading_trace_fault_fail_after cancel_type_set;
	struct threading_trace_fault_fail_after cancel;
};

void threading_trace_fault_init(void);
void threading_trace_fault_destroy(void);

void threading_trace_fault_verify(void);
void threading_trace_fault_reset(void);

struct threading_trace_fault_sem_control *threading_trace_fault_sem_get(void);
struct threading_trace_fault_mutex_control *
threading_trace_fault_mutex_get(void);
struct threading_trace_fault_thread_control *
threading_trace_fault_thread_get(void);

#endif /* _FIBER_TEST_MOCK_THREADING_TRACE_FAULT_H */
