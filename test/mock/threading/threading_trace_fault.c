#include <limits.h>

#include "fiber.h"
#include "src/test_internal.h"
#include "src/threading.h"

#include "test/unity.h"
#include "threading_trace_fault.h"

#define LOCK_CONTROL(control_struct) \
	TEST_ASSERT_FALSE(           \
		threading_vtable.mutex_lock(&control_struct.count_lock))
#define UNLOCK_CONTROL(control_struct) \
	TEST_ASSERT_FALSE(             \
		threading_vtable.mutex_unlock(&control_struct.count_lock))
#define LOCK_TRACKER(mtx) TEST_ASSERT_FALSE(threading_vtable.mutex_lock(mtx))
#define UNLOCK_TRACKER(mtx) \
	TEST_ASSERT_FALSE(threading_vtable.mutex_unlock(mtx))

#define SEM_TRACE_MAX (32)
#define MUTEX_TRACE_MAX (32)
#define THREAD_TRACE_MAX (64)

/* These structs allow this module to track each threading_*
 * instance and the amount of function calls done on them.
 */
struct threading_trace_fault_sem_tracker {
	fiber_semaphore *sem;
};

struct threading_trace_fault_mutex_tracker {
	fiber_mutex *mutex;
	unsigned long lock_count;
	unsigned long unlock_count;
};

struct threading_trace_fault_thread_tracker {
	tid thread_id;
	unsigned long exit_count;
	unsigned long detach_count;
	unsigned long join_count;
	unsigned long cancel_count;
};

/* Same idea as alloc_trace, just throw it in an array and find a free slot.
 * "Wow this is awful" - You, probably.
 */
static struct threading_trace_fault_sem_tracker sem_tracker[SEM_TRACE_MAX] = {
	0
};
static struct threading_trace_fault_mutex_tracker
	mutex_tracker[MUTEX_TRACE_MAX] = { 0 };
static struct threading_trace_fault_thread_tracker
	thread_tracker[THREAD_TRACE_MAX] = { 0 };
/* Each *_tracker array is protected by a mutex */
static fiber_mutex sem_tracker_lock;
static fiber_mutex mutex_tracker_lock;
static fiber_mutex thread_tracker_lock;

struct threading_trace_fault_sem_control sem_control = { 0 };
struct threading_trace_fault_mutex_control mutex_control = { 0 };
struct threading_trace_fault_thread_control thread_control = { 0 };

/* This comes from the threading_*.c file. It should export its
 * functions if FIBER_THREADING_INTERCEPT is defined in the testing
 * environment.
 */
extern const struct fiber_threading_vtable threading_vtable;
#define FIBER_THREADING_INTERCEPT
#if defined(FIBER_THREADING_LIB_PTHREAD)
#include "src/threading_pthread.c"
#endif /* FIBER_THREADING_LIB_PTHREAD */

/** threading_trace_fault Control functions **/

void threading_trace_fault_init(void)
{
	int sem_con, mtx_con, thr_con;
	int sem_trc, mtx_trc, thr_trc;

	sem_con = threading_vtable.mutex_init(&sem_control.count_lock);
	mtx_con = threading_vtable.mutex_init(&mutex_control.count_lock);
	thr_con = threading_vtable.mutex_init(&thread_control.count_lock);
	sem_trc = threading_vtable.mutex_init(&sem_tracker_lock);
	mtx_trc = threading_vtable.mutex_init(&mutex_tracker_lock);
	thr_trc = threading_vtable.mutex_init(&thread_tracker_lock);

	TEST_ASSERT_FALSE_MESSAGE(
		sem_con || mtx_con || thr_con || sem_trc || mtx_trc || thr_trc,
		"Failed to initialize a mutex in threading_trace_fault_init.");

	threading_trace_fault_reset();
}

void threading_trace_fault_destroy(void)
{
	int sem_con, mtx_con, thr_con;
	int sem_trc, mtx_trc, thr_trc;

	sem_con = threading_vtable.mutex_destroy(&sem_control.count_lock);
	mtx_con = threading_vtable.mutex_destroy(&mutex_control.count_lock);
	thr_con = threading_vtable.mutex_destroy(&thread_control.count_lock);
	sem_trc = threading_vtable.mutex_destroy(&sem_tracker_lock);
	mtx_trc = threading_vtable.mutex_destroy(&mutex_tracker_lock);
	thr_trc = threading_vtable.mutex_destroy(&thread_tracker_lock);

	TEST_ASSERT_FALSE_MESSAGE(
		sem_con || mtx_con || thr_con || sem_trc || mtx_trc || thr_trc,
		"Failed to destroy a mutex in threading_trace_fault_destroy.");
}

/* The idea here is to ensure no resources were left in the list. */
void threading_trace_fault_verify(void)
{
	unsigned long i;
	unsigned long sem_count, mutex_count, thread_count;

	sem_count = mutex_count = thread_count = 0;

	for (i = 0; i < SEM_TRACE_MAX; ++i) {
		if (sem_tracker[i].sem != NULL) {
			++sem_count;
		}
	}
	for (i = 0; i < MUTEX_TRACE_MAX; ++i) {
		if (mutex_tracker[i].mutex != NULL) {
			++mutex_count;
		}
	}
	for (i = 0; i < THREAD_TRACE_MAX; ++i) {
		if (thread_tracker[i].thread_id != 0) {
			++thread_count;
		}
	}
	TEST_ASSERT_EQUAL_MESSAGE(
		0, sem_count,
		"threading_trace_fault_verify found a semaphore resource leak.");
	TEST_ASSERT_EQUAL_MESSAGE(
		0, mutex_count,
		"threading_trace_fault_verify found a mutex resource leak.");
	TEST_ASSERT_EQUAL_MESSAGE(
		0, thread_count,
		"threading_trace_fault_verify found a thread resource leak.");
}

void threading_trace_fault_reset(void)
{
	unsigned long i;

	/* Ok, this may be close to macro hell but it can't be that close :) */
#define FAIL_AFTER_START(s)                                       \
	((struct threading_trace_fault_fail_after *)((char *)&s + \
						     sizeof(fiber_mutex)))
#define FAIL_AFTER_NUM(s)                   \
	(sizeof(s) - sizeof(fiber_mutex)) / \
		sizeof(struct threading_trace_fault_fail_after)
#define RESET_CONTROL_STRUCT(s)                              \
	do {                                                 \
		unsigned long i;                             \
		struct threading_trace_fault_fail_after *fa; \
                                                             \
		fa = FAIL_AFTER_START(s);                    \
		for (i = 0; i < FAIL_AFTER_NUM(s); ++i) {    \
			fa[i].count = ULONG_MAX;             \
			fa[i].fail_res = 0;                  \
		}                                            \
	} while (0)

	RESET_CONTROL_STRUCT(sem_control);
	RESET_CONTROL_STRUCT(mutex_control);
	RESET_CONTROL_STRUCT(thread_control);
#undef RESET_CONTROL_STRUCT
#undef FAIL_AFTER_NUM
#undef FAIL_AFTER_START
	for (i = 0; i < SEM_TRACE_MAX; ++i) {
		sem_tracker[i].sem = NULL;
	}
	for (i = 0; i < MUTEX_TRACE_MAX; ++i) {
		mutex_tracker[i].mutex = NULL;
		mutex_tracker[i].lock_count = 0;
		mutex_tracker[i].unlock_count = 0;
	}
	for (i = 0; i < THREAD_TRACE_MAX; ++i) {
		thread_tracker[i].thread_id = 0;
		thread_tracker[i].exit_count = 0;
		thread_tracker[i].detach_count = 0;
		thread_tracker[i].join_count = 0;
		thread_tracker[i].cancel_count = 0;
	}
}

struct threading_trace_fault_sem_control *threading_trace_fault_sem_get(void)
{
	return &sem_control;
}

struct threading_trace_fault_mutex_control *
threading_trace_fault_mutex_get(void)
{
	return &mutex_control;
}

struct threading_trace_fault_thread_control *
threading_trace_fault_thread_get(void)
{
	return &thread_control;
}

/* Functions for inserting, updating, and removing/verifying trackers */

/* This is most definitely macro hell. Just scroll past this please */
#define TRACKER_INSERT(val, len, list, val_member, lock, invalid_val)                                        \
	do {                                                                                                 \
		unsigned long i;                                                                             \
		int added = 0;                                                                               \
		for (i = 0; i < len; ++i) {                                                                  \
			/* Make sure sem hasn't already initialized */                                       \
			if (list[i].val_member == val) {                                                     \
				UNLOCK_TRACKER(&lock);                                                       \
				TEST_FAIL_MESSAGE(#val                                                       \
						  " was initialized twice.");                                \
			}                                                                                    \
			if (list[i].val_member == invalid_val && !added) {                                   \
				list[i].val_member = val;                                                    \
				added = 1;                                                                   \
				/* Don't break in order to check other semaphores in the list */             \
			}                                                                                    \
		}                                                                                            \
		if (!added) {                                                                                \
			UNLOCK_TRACKER(&lock);                                                               \
			TEST_FAIL_MESSAGE(                                                                   \
				#val                                                                         \
				" len is too small and your bad implementation is finally failing carter!"); \
		}                                                                                            \
	} while (0)
#define TRACKER_REMOVE(i, val, len, list, val_member, lock)                \
	do {                                                               \
		for (i = 0; i < len; ++i) {                                \
			if (list[i].val_member != val) {                   \
				continue;                                  \
			}                                                  \
			break;                                             \
		}                                                          \
		if (i >= len) {                                            \
			UNLOCK_TRACKER(&lock);                             \
			TEST_FAIL_MESSAGE(                                 \
				#val                                       \
				" destroyed that was never initialized."); \
		}                                                          \
	} while (0)

/* Look how nice those macros make these functions look though */
static void sem_tracker_insert(fiber_semaphore *sem)
{
	TRACKER_INSERT(sem, SEM_TRACE_MAX, sem_tracker, sem, sem_tracker_lock,
		       NULL);
}

static void sem_tracker_remove(const fiber_semaphore *sem)
{
	unsigned long i;

	TRACKER_REMOVE(i, sem, SEM_TRACE_MAX, sem_tracker, sem,
		       sem_tracker_lock);

	/* i should now be at the element we should remove */
	sem_tracker[i].sem = NULL;
}

static void mutex_tracker_insert(fiber_mutex *mut)
{
	TRACKER_INSERT(mut, MUTEX_TRACE_MAX, mutex_tracker, mutex,
		       mutex_tracker_lock, NULL);
}

static struct threading_trace_fault_mutex_tracker *
mutex_tracker_get(const fiber_mutex *mut)
{
	unsigned long i;
	for (i = 0; i < MUTEX_TRACE_MAX; ++i) {
		if (mutex_tracker[i].mutex == mut) {
			return &mutex_tracker[i];
		}
	}
	UNLOCK_TRACKER(&mutex_tracker_lock);
	TEST_FAIL_MESSAGE(
		"attempted to update a mutex tracker on a mutex didn't exist. "
		"This probably means some resource tried to lock or unlock an uninitialized mutex.");
	return NULL;
}

static void mutex_tracker_remove(const fiber_mutex *mut)
{
	unsigned long i;

	TRACKER_REMOVE(i, mut, MUTEX_TRACE_MAX, mutex_tracker, mutex,
		       mutex_tracker_lock);

	/* i should now be at the list element we should remove */
	mutex_tracker[i].mutex = NULL;
	if (mutex_tracker[i].lock_count != mutex_tracker[i].unlock_count) {
		UNLOCK_TRACKER(&mutex_tracker_lock);
		TEST_ASSERT_EQUAL_MESSAGE(mutex_tracker[i].lock_count,
					  mutex_tracker[i].unlock_count,
					  "expected = lock_count");
	}
}

static void thread_tracker_insert(tid *thread_id)
{
	TRACKER_INSERT((*thread_id), THREAD_TRACE_MAX, thread_tracker,
		       thread_id, thread_tracker_lock, 0);
}

static struct threading_trace_fault_thread_tracker *
thread_tracker_get(const tid *thread_id)
{
	unsigned long i;
	for (i = 0; i < THREAD_TRACE_MAX; ++i) {
		if (thread_tracker[i].thread_id == *thread_id) {
			return &thread_tracker[i];
		}
	}
	UNLOCK_TRACKER(&thread_tracker_lock);
	TEST_FAIL_MESSAGE(
		"attempted to update a thread tracker when the thread didn't exist.\n"
		"This probably means some resource tried to operate on a thread that DNE.");
	return NULL;
}

static void thread_tracker_remove(const tid *thread_id)
{
	unsigned long i;
	unsigned long count_sum;
	int valid_pair;

	TRACKER_REMOVE(i, (*thread_id), THREAD_TRACE_MAX, thread_tracker,
		       thread_id, thread_tracker_lock);

	/* i should now be at the list element we should remove */
	thread_tracker[i].thread_id = 0;

	/* Fiber has the following two cases when threads are canceled:
         * 1. The user calls fiber_threads_remove which allows the thread to
         *    call fiber_thread_exit and fiber_thread_detach. This is a "graceful"
         *    exit.
         * 2. The user frees the pool and we cancel the threads with fiber_thread_cancel.
         *    This method joins the threads.
         */
	/* count_sum will be used to ensure only 2 are called */
	count_sum =
		thread_tracker[i].exit_count + thread_tracker[i].detach_count +
		thread_tracker[i].cancel_count + thread_tracker[i].join_count;

	/* valid_pair will be used to ensure the 2 calls are a valid pair */
	valid_pair = (thread_tracker[i].exit_count == 1 &&
		      thread_tracker[i].detach_count == 1) ||
		     (thread_tracker[i].cancel_count == 1 &&
		      thread_tracker[i].join_count == 1);
	if (count_sum != 2) {
		UNLOCK_TRACKER(&thread_tracker_lock);
		TEST_ASSERT_EQUAL(2, count_sum);
	}
	if (!valid_pair) {
		UNLOCK_TRACKER(&thread_tracker_lock);
		TEST_FAIL_MESSAGE(
			"thread calls were invalid due invalid pair.");
	}
}

/* Check if the function should fail on this call and return the error if so */
#define IF_FAIL_RETURN(control_struct, control_member)                 \
	do {                                                           \
		LOCK_CONTROL(control_struct);                          \
		if (control_struct.control_member.count == 0) {        \
			UNLOCK_CONTROL(control_struct);                \
			return control_struct.control_member.fail_res; \
		}                                                      \
		--control_struct.control_member.count;                 \
		UNLOCK_CONTROL(control_struct);                        \
	} while (0)

/** Semaphore functions **/

int fiber_sem_init(fiber_semaphore *sem, unsigned int initial_value)
{
	int res;

	IF_FAIL_RETURN(sem_control, init);
	res = threading_vtable.sem_init(sem, initial_value);
	/* If init fails don't count it because destroy should not be called */
	if (res == 0) {
		LOCK_TRACKER(&sem_tracker_lock);
		sem_tracker_insert(sem);
		UNLOCK_TRACKER(&sem_tracker_lock);
	}
	return res;
}

int fiber_sem_destroy(fiber_semaphore *sem)
{
	int res;

	IF_FAIL_RETURN(sem_control, destroy);
	res = threading_vtable.sem_destroy(sem);
	if (res == 0) {
		LOCK_TRACKER(&sem_tracker_lock);
		sem_tracker_remove(sem);
		UNLOCK_TRACKER(&sem_tracker_lock);
	}

	return res;
}

int fiber_sem_wait(fiber_semaphore *sem)
{
	IF_FAIL_RETURN(sem_control, wait);
	return threading_vtable.sem_wait(sem);
}

int fiber_sem_trywait(fiber_semaphore *sem)
{
	IF_FAIL_RETURN(sem_control, trywait);
	return threading_vtable.sem_trywait(sem);
}

int fiber_sem_post(fiber_semaphore *sem)
{
	IF_FAIL_RETURN(sem_control, post);
	return threading_vtable.sem_post(sem);
}

int fiber_sem_getvalue(fiber_semaphore *sem, int *value_out)
{
	IF_FAIL_RETURN(sem_control, getvalue);
	return threading_vtable.sem_getvalue(sem, value_out);
}

/** Mutex functions **/

int fiber_mutex_init(fiber_mutex *mut)
{
	int res;

	IF_FAIL_RETURN(mutex_control, init);
	res = threading_vtable.mutex_init(mut);
	if (res == 0) {
		LOCK_TRACKER(&mutex_tracker_lock);
		mutex_tracker_insert(mut);
		UNLOCK_TRACKER(&mutex_tracker_lock);
	}
	return res;
}

int fiber_mutex_destroy(fiber_mutex *mut)
{
	int res;

	IF_FAIL_RETURN(mutex_control, destroy);
	res = threading_vtable.mutex_destroy(mut);
	if (res == 0) {
		LOCK_TRACKER(&mutex_tracker_lock);
		mutex_tracker_remove(mut);
		UNLOCK_TRACKER(&mutex_tracker_lock);
	}
	return res;
}

int fiber_mutex_lock(fiber_mutex *mut)
{
	int res;
	struct threading_trace_fault_mutex_tracker *tracker;

	IF_FAIL_RETURN(mutex_control, lock);
	res = threading_vtable.mutex_lock(mut);
	if (res == 0) {
		LOCK_TRACKER(&mutex_tracker_lock);
		/* Function fails if would return NULL */
		tracker = mutex_tracker_get(mut);
		++tracker->lock_count;
		UNLOCK_TRACKER(&mutex_tracker_lock);
	}

	return res;
}

int fiber_mutex_unlock(fiber_mutex *mut)
{
	int res;
	struct threading_trace_fault_mutex_tracker *tracker;

	IF_FAIL_RETURN(mutex_control, unlock);
	res = threading_vtable.mutex_unlock(mut);
	if (res == 0) {
		LOCK_TRACKER(&mutex_tracker_lock);
		/* Function fails if would return NULL */
		tracker = mutex_tracker_get(mut);
		++tracker->unlock_count;
		UNLOCK_TRACKER(&mutex_tracker_lock);
	}

	return res;
}

/** Thread functions **/

int fiber_thread_create(tid *thread_id, fiber_job_function_t runner, void *arg)
{
	int res;

	IF_FAIL_RETURN(thread_control, create);
	res = threading_vtable.thread_create(thread_id, runner, arg);
	if (res == 0) {
		LOCK_TRACKER(&thread_tracker_lock);
		thread_tracker_insert(thread_id);
		UNLOCK_TRACKER(&thread_tracker_lock);
	}

	return res;
}

void fiber_thread_exit(void *ret_val)
{
	tid caller_thread_id = 0;
#if defined(FIBER_THREADING_LIB_PTHREAD)
	caller_thread_id = pthread_self();
#else
#error "threading_trace_fault.c only implements a way to get the current thread's id for pthreads"
#endif
	threading_vtable.thread_exit(ret_val);
	LOCK_TRACKER(&thread_tracker_lock);
	thread_tracker_remove(&caller_thread_id);
	UNLOCK_TRACKER(&thread_tracker_lock);
}

int fiber_thread_detach(const tid *thread_id)
{
	int res;
	struct threading_trace_fault_thread_tracker *tracker;

	IF_FAIL_RETURN(thread_control, detach);
	res = threading_vtable.thread_detach(thread_id);
	if (res == 0) {
		LOCK_TRACKER(&thread_tracker_lock);
		tracker = thread_tracker_get(thread_id);
		++tracker->detach_count;
		UNLOCK_TRACKER(&thread_tracker_lock);
	}
	return res;
}

int fiber_thread_join(const tid *thread_id, void **ret_val)
{
	int res;

	IF_FAIL_RETURN(thread_control, join);
	res = threading_vtable.thread_join(thread_id, ret_val);
	if (res == 0) {
		LOCK_TRACKER(&thread_tracker_lock);
		thread_tracker_remove(thread_id);
		UNLOCK_TRACKER(&thread_tracker_lock);
	}
	return res;
}

int fiber_thread_cancel_enable(void)
{
	IF_FAIL_RETURN(thread_control, cancel_enable);
	return threading_vtable.thread_cancel_enable();
}

int fiber_thread_cancel_disable(void)
{
	IF_FAIL_RETURN(thread_control, cancel_disable);
	return threading_vtable.thread_cancel_disable();
}

int fiber_thread_cancel_type_set(int cancel_type)
{
	IF_FAIL_RETURN(thread_control, cancel_type_set);
	return threading_vtable.thread_cancel_type_set(cancel_type);
}

int fiber_thread_cancel(const tid *thread_id)
{
	int res;
	struct threading_trace_fault_thread_tracker *tracker;

	IF_FAIL_RETURN(thread_control, cancel);
	res = threading_vtable.thread_cancel(thread_id);
	if (res == 0) {
		LOCK_TRACKER(&thread_tracker_lock);
		tracker = thread_tracker_get(thread_id);
		++tracker->cancel_count;
		UNLOCK_TRACKER(&thread_tracker_lock);
	}
	return res;
}
