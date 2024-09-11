#include <errno.h>
#include <semaphore.h>

#include "../../job_queue.h"
#include "../../queue_impls/fifo_job_queue.h"
#include "../xtal.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void setup(qsize cap);
static void teardown();
static struct fifo_jq *jq = NULL;

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
	ASSERT_EQUAL_INT(EAGAIN, res)
	teardown();
}

void sigphony(int signum)
{
}
TEST(fifo_pop_empty_block)
{
	setup(1);
	struct fiber_job buf;
	int f = fork();
	if (f < 0) {
		FAIL("Fork failed.");
	} else if (f == 0) {
		/* This is a little messy but we are testing two things:
                 * 1. fiber_queue_fifo_pop blocks when empty.
                 * 2. We can wake fiber_queue_fifo_pop up with a signal.
                 *    This is needed for handling events like removing threads.
                 */
		struct sigaction sa = { 0 };
		sigemptyset(&sa.sa_mask);
		sa.sa_handler = sigphony;
		sigaction(SIGUSR1, &sa, NULL);
		int res = fiber_queue_fifo_pop(jq, &buf, FIBER_BLOCK);
		exit(res);
	} else {
		// Hacky but just wait long enough for child to call sem_wait
		// Tell it to wake up
		sleep(1);
		kill(f, SIGUSR1);
		int child_stat = -1;
		waitpid(f, &child_stat, 0);
		ASSERT_EQUAL_INT(EINTR, WEXITSTATUS(child_stat));
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
