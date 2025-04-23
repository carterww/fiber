/* See LICENSE file for copyright and license details. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

#include "fiber.h"
#include "fiber_fifo.h"

/* Helper macro to make exiting on error easier */
#define EXIT_ERR(err)      \
	if (err != 0) {    \
		exit(err); \
	}

#define FIBER_JOB_QUEUE_LENGTH (1024)
static const int fib_to = 100000;
static const qsize jobs_push_num = FIBER_JOB_QUEUE_LENGTH * 16;
/* Must be in increasing order */
static const tpsize threads_num[] = { 1, 2, 4, 8, 16, 32 };
static const unsigned long threads_num_len =
	sizeof(threads_num) / sizeof(*threads_num);

/* Helper function for timing runs with different thread sizes */
static suseconds_t time_usec_now(void);

/* Checks if the compiled version of Fiber is compatible with this program */
static int example_compatible_fiber(void);

/* Helper function that calls fiber_init with correct options */
static struct fiber_pool *pool_init(tpsize threads_num, qsize queue_length);

static void pretty_print_result(tpsize threads_num, suseconds_t duration);

/* This is the function that will be called invoked by a thread in the pool.
 * For this example, it just performs some CPU bound busy work.
 */
static void *fib_runner(void *arg)
{
	unsigned long a = 0;
	unsigned long b = 1;
	int i = 1;

	(void)arg;
	/* I know b isn't the real fib due to overflow. This is just busy work */
	while (i < fib_to) {
		unsigned long c = a + b;
		a = b;
		b = c;
		++i;
	}
	return (void *)b;
}

int main(void)
{
	unsigned long i;
	int example_compatible;
	int error;
	struct fiber_pool *pool;

	example_compatible = example_compatible_fiber();
	if (!example_compatible) {
		printf("The compiled version of Fiber is not compatible with the example.\n "
		       "Either the version is incompatible or the fifo queue was not compiled into Fiber.\n");
		return 1;
	}

	pool = pool_init(threads_num[0], FIBER_JOB_QUEUE_LENGTH);

	for (i = 0; i < threads_num_len; ++i) {
		qsize j;
		suseconds_t start, end;
		/* Add threads to pool for next run */
		if (i != 0) {
			/* The threads are not guaranteed to be running after this
                         * call returns but it should be good enough.
                         */
			error = fiber_threads_add(
				pool, threads_num[i] - threads_num[i - 1]);
			EXIT_ERR(error);
		}

		start = time_usec_now();
		for (j = 0; j < jobs_push_num; ++j) {
			/* Create the job that Fiber should execute. Fiber will
                         * copy the job into its own data structures and assign
                         * it a Job Id.
                         */
			struct fiber_job job = { 0, fib_runner, NULL };

			/* Here we push the job onto the job queue and get its
                         * job id. A Job Id < 0 indicates an error. See the comment
                         * above fiber_job_push in fiber.h for possible errors.
                         */
			jid job_id =
				fiber_job_push(pool, &job, FIBER_QUEUE_BLOCK);
			if (job_id < 0) {
				printf("Received error from fiber_job_push\n");
				return 1;
			}
		}

		/* fiber_wait allows the caller to block until all jobs
                 * in the pool have finished. Be very careful when calling
                 * this if other threads or jobs can push jobs.
                 */
		error = fiber_wait(pool);
		EXIT_ERR(error);
		end = time_usec_now();

		pretty_print_result(threads_num[i], end - start);
	}

	/* This will cancel all the threads and free the pool's other
         * resources.
         */
	fiber_free(pool);
	return 0;
}

static suseconds_t time_usec_now(void)
{
	static suseconds_t us_per_sec = 1000000;
	static struct timeval now;
	gettimeofday(&now, NULL);

	return now.tv_sec * us_per_sec + now.tv_usec;
}

static int example_compatible_fiber(void)
{
	int fifo_capable;
	int version_compatible;

	/* fiber_capability_get allows the program to check if a certain feature
         * was compiled into the Fiber. In this case, we are checking if the queue
         * implementation we'd like to use is available.
         */
	fifo_capable = fiber_capability_get(FIBER_CAPABILITY_FIBER_FIFO_QUEUE);

	/* fiber_libversion_compatible allows the program to check if the compiled
         * version of Fiber is compatible with the version specified in fiber.h.
         */
	version_compatible = fiber_libversion_compatible();

	return fifo_capable && version_compatible;
}

static struct fiber_pool *pool_init(tpsize tnum, qsize queue_length)
{
	struct fiber_pool_init_options pool_opts;
	struct fiber_queue_operations queue_ops = FIBER_FIFO_QUEUE_OPERATIONS;
	struct fiber_init_result init_res;

	/* Specify the options for creating a new pool. See the comment
         * above fiber_init in fiber.h for an explanation of each.
         */
	pool_opts.queue_ops = &queue_ops;
	pool_opts.malloc = malloc; /* Use libc malloc */
	pool_opts.free = free; /* Use libc free */
	pool_opts.queue_length = queue_length;
	pool_opts.threads_number = tnum;

	init_res = fiber_init(&pool_opts);
	if (init_res.error != 0) {
		printf("Error initializing the pool.\n");
		exit(1);
	}
	return init_res.pool;
}

static void pretty_print_result(tpsize tnum, suseconds_t duration)
{
	static char buff[32];
	char *num_start;
	suseconds_t remaining;
	unsigned long i;

	memset(buff, 0, (size_t)32);
	/* Start out of range (at term char) */
	num_start = &buff[31];
	remaining = duration;
	i = 0;

	while (num_start >= buff) {
		char digit = (char)((remaining % 10) + '0');
		remaining = remaining / 10;

		--num_start;
		*num_start = digit;

		if (remaining == 0) {
			break;
		}

		++i;
		if (i % 3 == 0) {
			--num_start;
			*num_start = ',';
		}
	}

	printf("Time to run %d jobs with %3d threads:  %13s us\n",
	       jobs_push_num, tnum, num_start);
}
