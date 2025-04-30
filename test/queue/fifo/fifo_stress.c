#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fiber/fiber.h"
#include "fiber/fiber_fifo.h"
#include "fiber_atomic/atomic.h"
#include "fiber_lock/semaphore.h"
#include "src/queue/fifo_internal.h"
#include "src/threading.h"

#include "test/unity.h"
#include "test/mock/threading/threading_trace_fault.h"

#define QUEUE_LENGTH (4096)

#define QUEUE_JOB_PUSH_ITERATIONS (48)
#define QUEUE_JOB_PUSH_PER_THREAD (1696)

#define NUM_PUSHERS (4)

static struct fiber_fifo_jq *jq = NULL;

static jid job_id_counter;

static struct fiber_job pushed_jobs[QUEUE_JOB_PUSH_PER_THREAD * NUM_PUSHERS];
static size_t pushed_jobs_idx;
static struct fiber_job popped_jobs[QUEUE_JOB_PUSH_PER_THREAD * NUM_PUSHERS];
static size_t popped_jobs_idx;

static fiber_semaphore push_start_sem;
static fiber_semaphore push_end_sem;
static fiber_semaphore pop_start_sem;
static fiber_semaphore pop_end_sem;

void setUp(void)
{
	struct fiber_queue_init_result res;

	threading_trace_fault_reset();
	res = fiber_queue_fifo_init(QUEUE_LENGTH, malloc, free);
	TEST_ASSERT_EQUAL(0, res.error);
	TEST_ASSERT_NOT_NULL(res.queue);
	jq = (struct fiber_fifo_jq *)res.queue;

	job_id_counter = 1;
	memset(pushed_jobs, 0, sizeof(pushed_jobs));
	pushed_jobs_idx = 0;
	memset(popped_jobs, 0, sizeof(popped_jobs));
	popped_jobs_idx = 0;
}

void tearDown(void)
{
	if (jq != NULL) {
		fiber_queue_fifo_free(jq);
	}
	threading_trace_fault_verify();
}

static void *random_invalid_ptr(void)
{
	return (void *)(unsigned long)rand();
}

static void test_fifo_push_pop_stress_push_body(void)
{
	struct fiber_job job;
	size_t open_pushed_jobs_idx;
	int res;

	job.job_id =
		fiber_atomic_fetch_inc(&job_id_counter, FIBER_ATOMIC_ACQ_REL);
	job.job_func = (fiber_job_function_t)random_invalid_ptr();
	job.job_arg = random_invalid_ptr();

	res = fiber_queue_fifo_push(jq, &job, FIBER_QUEUE_BLOCK);
	TEST_ASSERT_EQUAL(0, res);

	open_pushed_jobs_idx =
		fiber_atomic_fetch_inc(&pushed_jobs_idx, FIBER_ATOMIC_ACQ_REL);
	TEST_ASSERT_LESS_THAN(QUEUE_JOB_PUSH_PER_THREAD * NUM_PUSHERS,
			      open_pushed_jobs_idx);
	pushed_jobs[open_pushed_jobs_idx] = job;
}

static void *test_fifo_push_pop_stress_push_runner(void *arg)
{
	jid i, j;
	(void)arg;

	srand((unsigned int)time(NULL));
	for (i = 0; i < QUEUE_JOB_PUSH_ITERATIONS; ++i) {
		TEST_ASSERT_FALSE(fiber_sem_wait_fn_ptr(&push_start_sem));
		for (j = 0; j < QUEUE_JOB_PUSH_PER_THREAD; ++j) {
			test_fifo_push_pop_stress_push_body();
		}
		TEST_ASSERT_FALSE(fiber_sem_post_fn_ptr(&push_end_sem));
	}

	pthread_exit(NULL);
	return NULL;
}

static void test_fifo_push_pop_stress_pop_body(void)
{
	struct fiber_job job;
	size_t open_popped_jobs_idx;
	int res;

	res = fiber_queue_fifo_pop(jq, &job, FIBER_QUEUE_BLOCK);
	TEST_ASSERT_EQUAL(0, res);
	TEST_ASSERT_NOT_EQUAL(0, job.job_id);

	open_popped_jobs_idx =
		fiber_atomic_fetch_inc(&popped_jobs_idx, FIBER_ATOMIC_ACQ_REL);
	TEST_ASSERT_LESS_THAN(QUEUE_JOB_PUSH_PER_THREAD * NUM_PUSHERS,
			      open_popped_jobs_idx);
	popped_jobs[open_popped_jobs_idx] = job;
}

static void *test_fifo_push_pop_stress_pop_runner(void *arg)
{
	jid i, j;
	(void)arg;

	for (i = 0; i < QUEUE_JOB_PUSH_ITERATIONS; ++i) {
		TEST_ASSERT_FALSE(fiber_sem_wait_fn_ptr(&pop_start_sem));
		for (j = 0; j < QUEUE_JOB_PUSH_PER_THREAD; ++j) {
			test_fifo_push_pop_stress_pop_body();
		}
		TEST_ASSERT_FALSE(fiber_sem_post_fn_ptr(&pop_end_sem));
	}

	pthread_exit(NULL);
	return NULL;
}

static void verify_iteration_and_reset(void)
{
	size_t pop_i;
	size_t push_i;
	size_t push_global_idx;
	size_t pop_global_idx;

	push_global_idx =
		fiber_atomic_load(&pushed_jobs_idx, FIBER_ATOMIC_ACQUIRE);
	pop_global_idx =
		fiber_atomic_load(&popped_jobs_idx, FIBER_ATOMIC_ACQUIRE);

	/* Make sure every job was pushed and popped */
	TEST_ASSERT_EQUAL(QUEUE_JOB_PUSH_PER_THREAD * NUM_PUSHERS,
			  push_global_idx);
	TEST_ASSERT_EQUAL(QUEUE_JOB_PUSH_PER_THREAD * NUM_PUSHERS,
			  pop_global_idx);

	/* Verify jobs were not mutated and every job is in popped list.
	 * Yes yes O(n^2).
	 */
	for (pop_i = 0; pop_i < QUEUE_JOB_PUSH_PER_THREAD * NUM_PUSHERS;
	     ++pop_i) {
		struct fiber_job *pop_job = &popped_jobs[pop_i];
		int found = 0;
		for (push_i = 0;
		     push_i < QUEUE_JOB_PUSH_PER_THREAD * NUM_PUSHERS;
		     ++push_i) {
			struct fiber_job *push_job = &pushed_jobs[push_i];
			if (push_job->job_id != pop_job->job_id) {
				continue;
			}
			TEST_ASSERT_EQUAL_MESSAGE(pop_job->job_func,
						  push_job->job_func,
						  "job_func messed up");
			TEST_ASSERT_EQUAL_MESSAGE(pop_job->job_arg,
						  push_job->job_arg,
						  "job_arg messed up");
			found = 1;
		}
		/* ERROR: Failing here sometimes for some reason */
		TEST_ASSERT_EQUAL(1, found);
	}
	fiber_atomic_store(&pushed_jobs_idx, 0, FIBER_ATOMIC_RELEASE);
	fiber_atomic_store(&popped_jobs_idx, 0, FIBER_ATOMIC_RELEASE);
}

void test_fifo_push_pop_stress(void)
{
	tid pop_tids[NUM_PUSHERS];
	tid push_tids[NUM_PUSHERS];
	size_t i;

	TEST_ASSERT_FALSE(fiber_sem_init_fn_ptr(&push_start_sem, 0));
	TEST_ASSERT_FALSE(fiber_sem_init_fn_ptr(&push_end_sem, 0));
	TEST_ASSERT_FALSE(fiber_sem_init_fn_ptr(&pop_start_sem, 0));
	TEST_ASSERT_FALSE(fiber_sem_init_fn_ptr(&pop_end_sem, 0));

	for (i = 0; i < NUM_PUSHERS; ++i) {
		TEST_ASSERT_FALSE(fiber_thread_create_fn_ptr(
			&pop_tids[i], test_fifo_push_pop_stress_pop_runner,
			NULL));
		TEST_ASSERT_FALSE(fiber_thread_create_fn_ptr(
			&push_tids[i], test_fifo_push_pop_stress_push_runner,
			NULL));
	}

	for (i = 0; i < QUEUE_JOB_PUSH_ITERATIONS; ++i) {
		size_t j;

		for (j = 0; j < NUM_PUSHERS; ++j) {
			TEST_ASSERT_FALSE(
				fiber_sem_post_fn_ptr(&push_start_sem));
			TEST_ASSERT_FALSE(
				fiber_sem_post_fn_ptr(&pop_start_sem));
		}
		for (j = 0; j < NUM_PUSHERS; ++j) {
			TEST_ASSERT_FALSE(fiber_sem_wait_fn_ptr(&push_end_sem));
			TEST_ASSERT_FALSE(fiber_sem_wait_fn_ptr(&pop_end_sem));
		}
		verify_iteration_and_reset();
	}

	for (i = 0; i < NUM_PUSHERS; ++i) {
		void *res;
		TEST_ASSERT_FALSE(
			fiber_thread_join_fn_ptr(&push_tids[i], &res));
		TEST_ASSERT_FALSE(fiber_thread_join_fn_ptr(&pop_tids[i], &res));
		(void)res;
	}

	TEST_ASSERT_FALSE(fiber_sem_destroy_fn_ptr(&push_start_sem));
	TEST_ASSERT_FALSE(fiber_sem_destroy_fn_ptr(&push_end_sem));
	TEST_ASSERT_FALSE(fiber_sem_destroy_fn_ptr(&pop_start_sem));
	TEST_ASSERT_FALSE(fiber_sem_destroy_fn_ptr(&pop_end_sem));
}

int main(void)
{
	UNITY_BEGIN();
	threading_trace_fault_init();

	RUN_TEST(test_fifo_push_pop_stress);

	threading_trace_fault_destroy();
	return UNITY_END();
}
