#include <errno.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "job_queue.h"
#include "queue_impls/fifo_job_queue.h"
#include "xtal.h"

static void setup(qsize cap);
static void teardown();

static struct fifo_jq *jq = NULL;
void *do_nothing(void *arg)
{
}

static void push_phony_job(jid id)
{
	struct fiber_job test = {
		.job_id = id,
	};
	jq->tail = 1;
	*jq->jobs = test;
	sem_post(&jq->jobs_num);
}

TEST(fifo_init)
{
	setup(5);
	ASSERT_EQUAL_INT(0, jq->head)
	ASSERT_EQUAL_INT(0, jq->tail)
	ASSERT_EQUAL_INT(5, jq->capacity)
	int semval = -1;
	sem_getvalue(&jq->jobs_num, &semval);
	ASSERT_EQUAL_INT(0, semval)
	sem_getvalue(&jq->void_num, &semval);
	ASSERT_EQUAL_INT(5, semval)
	teardown();
}

TEST(fifo_push_empty)
{
	setup(5);
	// Note: this call doesn't check if job is valid, fiber_push
	// does that
	struct fiber_job job = { 0 };
	job.job_func = do_nothing;
	int res = fiber_queue_fifo_push(jq, &job, FIBER_BLOCK);
	ASSERT_EQUAL_INT(0, res)
	ASSERT_EQUAL_INT(0, jq->head)
	ASSERT_EQUAL_INT(1, jq->tail)
	int semval = -1;
	sem_getvalue(&jq->jobs_num, &semval);
	ASSERT_EQUAL_INT(1, semval)
	sem_getvalue(&jq->void_num, &semval);
	ASSERT_EQUAL_INT(4, semval)
	teardown();
}

TEST(fifo_push_full)
{
	setup(2);
	struct fiber_job j = { 0 };
	j.job_func = do_nothing;
	for (int i = 0; i < 2; i++) {
		int res = fiber_queue_fifo_push(jq, &j, FIBER_NO_BLOCK);
		ASSERT_EQUAL_INT(0, res)
	}
	ASSERT_EQUAL_INT(0, jq->head)
	ASSERT_EQUAL_INT(0, jq->tail)
	int semval = -1;
	sem_getvalue(&jq->jobs_num, &semval);
	ASSERT_EQUAL_INT(2, semval)
	sem_getvalue(&jq->void_num, &semval);
	ASSERT_EQUAL_INT(0, semval)
	int res = fiber_queue_fifo_push(jq, &j, FIBER_NO_BLOCK);
	ASSERT_EQUAL_INT(-EAGAIN, res)
	teardown();
}

static void *__do_nothing_job(void *arg)
{
	return NULL;
}

static struct fiber_job wake_job = { .job_id = JOB_ID_MIN,
				     .job_func = __do_nothing_job,
				     .job_arg = NULL };
TEST(fifo_pop_empty_block)
{
	setup(1);
	struct fiber_job buf;
	int f = fork();
	/* This test is a little scufffed. It's goal is to ensure
         * exit(8) is never called because fiber_queue_fifo_pop
         * will block. alarm(1) will terminate the process after
         * 1 second. This assumes sigalrm will term process with
         * exit not equal to 8.
         */
	if (f < 0) {
		FAIL("Fork failed.");
	} else if (f == 0) {
		alarm(1);
		fiber_queue_fifo_pop(jq, &buf, FIBER_BLOCK);
		exit(8);
	} else {
		int child_stat = -1;
		waitpid(f, &child_stat, 0);
		int exit_stat = WEXITSTATUS(child_stat);
		if (exit_stat == 8) {
			FAIL("Made it past fiber_queue_fifo_pop");
		}
	}
	teardown();
}

TEST(fifo_pop_empty_noblock)
{
	setup(1);
	struct fiber_job buf;
	int res = fiber_queue_fifo_pop(jq, &buf, FIBER_NO_BLOCK);
	ASSERT_EQUAL_INT(EAGAIN, res)
	teardown();
}

TEST(fifo_pop_block)
{
	setup(2);
	push_phony_job(0xB00B);
	struct fiber_job buf;
	int res = fiber_queue_fifo_pop(jq, &buf, FIBER_BLOCK);
	ASSERT_EQUAL_INT(0, res)
	ASSERT_EQUAL_LONG((long)0xB00B, buf.job_id)
	teardown();
}

TEST(fifo_pop_noblock)
{
	setup(2);
	push_phony_job(0xB00B);
	struct fiber_job buf;
	int res = fiber_queue_fifo_pop(jq, &buf, FIBER_BLOCK);
	ASSERT_EQUAL_INT(0, res)
	ASSERT_EQUAL_LONG((long)0xB00B, buf.job_id)
	teardown();
}

int main()
{
	run_tests();
	return 0;
}

static void setup(qsize cap)
{
	int res = fiber_queue_fifo_init((void **)&jq, cap, malloc, free);
	ASSERT_EQUAL_INT(0, res);
}

static void teardown()
{
	fiber_queue_fifo_free(jq);
	jq = NULL;
}
