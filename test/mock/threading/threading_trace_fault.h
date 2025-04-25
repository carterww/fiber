#ifndef _FIBER_TEST_MOCK_THREADING_TRACE_FAULT_H
#define _FIBER_TEST_MOCK_THREADING_TRACE_FAULT_H

#include "fiber_lock/mutex.h"

struct threading_trace_fault_fail_after {
	unsigned long count;
	int fail_res;
};

#define TRACE_FAULT_ARR_LENGTH(comp_struct) \
	(sizeof(comp_struct) / sizeof(struct threading_trace_fault_fail_after))

struct threading_trace_fault_sem_control_components {
	struct threading_trace_fault_fail_after init;
	struct threading_trace_fault_fail_after destroy;
	struct threading_trace_fault_fail_after wait;
	struct threading_trace_fault_fail_after trywait;
	struct threading_trace_fault_fail_after post;
	struct threading_trace_fault_fail_after getvalue;
};

union threading_trace_fault_sem_control_union {
	struct threading_trace_fault_fail_after arr[TRACE_FAULT_ARR_LENGTH(
		struct threading_trace_fault_sem_control_components)];
	struct threading_trace_fault_sem_control_components comp;
};

struct threading_trace_fault_sem_control {
	fiber_mutex count_lock;
	union threading_trace_fault_sem_control_union failers;
};

struct threading_trace_fault_mutex_control_components {
	struct threading_trace_fault_fail_after init;
	struct threading_trace_fault_fail_after destroy;
	struct threading_trace_fault_fail_after lock;
	struct threading_trace_fault_fail_after unlock;
};

union threading_trace_fault_mutex_control_union {
	struct threading_trace_fault_fail_after arr[TRACE_FAULT_ARR_LENGTH(
		struct threading_trace_fault_mutex_control_components)];
	struct threading_trace_fault_mutex_control_components comp;
};

struct threading_trace_fault_mutex_control {
	fiber_mutex count_lock;
	union threading_trace_fault_mutex_control_union failers;
};

struct threading_trace_fault_thread_control_components {
	struct threading_trace_fault_fail_after create;
	struct threading_trace_fault_fail_after detach;
	struct threading_trace_fault_fail_after join;
	struct threading_trace_fault_fail_after cancel_enable;
	struct threading_trace_fault_fail_after cancel_disable;
	struct threading_trace_fault_fail_after cancel_type_set;
	struct threading_trace_fault_fail_after cancel;
};

union threading_trace_fault_thread_control_union {
	struct threading_trace_fault_fail_after arr[TRACE_FAULT_ARR_LENGTH(
		struct threading_trace_fault_thread_control_components)];
	struct threading_trace_fault_thread_control_components comp;
};

struct threading_trace_fault_thread_control {
	fiber_mutex count_lock;
	union threading_trace_fault_thread_control_union failers;
};

#undef TRACE_FAULT_ARR_LENGTH

void threading_trace_fault_init(void);
void threading_trace_fault_destroy(void);

void threading_trace_fault_verify(void);
void threading_trace_fault_reset(void);

struct threading_trace_fault_sem_control_components *
threading_trace_fault_sem_get(void);
struct threading_trace_fault_mutex_control_components *
threading_trace_fault_mutex_get(void);
struct threading_trace_fault_thread_control_components *
threading_trace_fault_thread_get(void);

#endif /* _FIBER_TEST_MOCK_THREADING_TRACE_FAULT_H */
