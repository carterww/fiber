/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_TEST_INTERNAL_H
#define _FIBER_TEST_INTERNAL_H

/* This file contains struct definitions and extern variables that allow the
 * testing code to peek into the internals of each module. Each struct corresponds
 * to a file and the struct exposes static functions and variables. This will allow
 * the testing code to test static functions without needing to include the C source
 * files. Each file will be responsible for defining the contents of the struct.
 */

/* Only provide declarations if the ENV is test. No production code should need
 * anything from this header.
 */
#if defined(FIBER_BUILD_ENV_TEST)

#include "bitstring.h"
#include "fiber/fiber.h"
#include "fiber_atomic/atomic.h"
#include "fiber_internal.h"
#include "fiber_lock/semaphore.h"
#include "worker.h"

struct fiber_test_internal_capability {
	struct fiber_bitstring *capability_bitstring;
};

struct fiber_test_internal_fiber {
	int (*fiber_validate_init_options)(
		const struct fiber_pool_init_options *opts);

	int (*fiber_init_queue)(struct fiber_pool *pool,
				const struct fiber_pool_init_options *opts);
	void (*fiber_free_queue)(struct fiber_pool *pool);

	jid (*fiber_fetch_next_jid)(jid *job_id_prev);

	int (*fiber_thread_pool_start_threads)(struct fiber_pool *pool,
					       tpsize threads_number);
};

struct fiber_test_internal_queue_fifo {
	void (*fiber_queue_sem_wait)(fiber_semaphore *sem);
	int (*fiber_queue_sem_trywait)(fiber_semaphore *sem);
	qsize (*fiber_queue_fetch_increment)(qsize *target, qsize cap);
};

#if defined(FIBER_THREADING_LIB_PTHREAD)
struct fiber_test_internal_threading_pthread {
	int (*__fiber_thread_setcancelstate)(int state);
};
#endif /* FIBER_THREADING_LIB_PTHREAD */

struct fiber_test_internal_version {
	const struct fiber_version *libversion;
};

struct fiber_test_internal_worker {
	void (*fiber_worker_runner_cleanup)(void *fiber_worker_thread_arg);
	void (*__fiber_worker_runner_cleanup)(
		struct fiber_worker_thread_arg *arg);
	void (*fiber_worker_loop)(struct fiber_pool *pool,
				  struct fiber_thread *thread);
	void (*fiber_worker_execute_job)(struct fiber_pool *pool,
					 struct fiber_thread *thread,
					 struct fiber_job *job);
	int (*fiber_worker_should_handle_flag_kill)(
		const struct fiber_pool *pool,
		enum fiber_atomic_memorder load_memorder);
	int (*fiber_worker_should_handle_flag_wait)(
		const struct fiber_pool *pool,
		enum fiber_atomic_memorder load_memorder);
	int (*fiber_worker_handle_flag_kill)(struct fiber_pool *pool);
	void (*fiber_worker_handle_flag_wait)(struct fiber_pool *pool);

	void *(*fiber_wake_runner)(void *arg);
};

extern struct fiber_test_internal_capability fiber_test_internal_capability;
extern struct fiber_test_internal_fiber fiber_test_internal_fiber;
extern struct fiber_test_internal_queue_fifo fiber_test_internal_queue_fifo;
#if defined(FIBER_THREADING_LIB_PTHREAD)
extern struct fiber_test_internal_threading_pthread
	fiber_test_internal_threading_pthread;
#endif /* FIBER_THREADING_LIB_PTHREAD */
extern struct fiber_test_internal_version fiber_test_internal_version;
extern struct fiber_test_internal_worker fiber_test_internal_worker;

#endif /* FIBER_BUILD_ENV_TEST */
#endif /* _FIBER_TEST_INTERNAL_H */
