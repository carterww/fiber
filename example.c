#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

#include <fbr.h>
#include <fbr_errno.h>
#include <fbr_jq_ring.h>

/* Helper macro to make exiting on error easier */
#define EXIT_ERR(err)         \
	if (err != FBR_EOK) { \
		exit(err);    \
	}

#define FIBER_JOB_QUEUE_LENGTH (1024)
static const int fib_to = 100000;
static const uint32_t jobs_push_num = FIBER_JOB_QUEUE_LENGTH * 16;
/* Must be in increasing order */
static const uint32_t threads_num[] = { 1, 2, 4, 8, 16, 32 };
static const unsigned long threads_num_len =
	sizeof(threads_num) / sizeof(*threads_num);

/* Helper function for timing runs with different thread sizes */
static suseconds_t time_usec_now(void);

/* Helper function that calls fiber_init with correct options */
static struct fbr_pool *pool_init(uint32_t threads_num, uint32_t queue_length);

static void pretty_print_result(uint32_t threads_num, suseconds_t duration);

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
	uint64_t job_id_counter = 0;
	unsigned long i;
	fbr_errno_t error;
	struct fbr_pool *pool;

	pool = pool_init(threads_num[0], FIBER_JOB_QUEUE_LENGTH);

	for (i = 0; i < threads_num_len; ++i) {
		uint32_t j;
		suseconds_t start, end;
		/* Add threads to pool for next run */
		if (i != 0) {
			/* The threads are not guaranteed to be running after this
                         * call returns but it should be good enough.
                         */
			uint32_t tnum = threads_num[i] - threads_num[i - 1];
			error = fbr_thread_add(pool, &tnum);
			EXIT_ERR(error);
			if (tnum != threads_num[i] - threads_num[i - 1]) {
				printf("Failed to start %u threads\n",
				       threads_num[i] - threads_num[i - 1]);
				exit(1);
			}
		}

		start = time_usec_now();
		for (j = 0; j < jobs_push_num; ++j) {
			/* Create the job that Fiber should execute. Fiber will
                         * copy the job into its own data structures and assign
                         * it a Job Id.
                         */
			struct fbr_job job = { job_id_counter++, fib_runner,
					       NULL };

			/* Here we push the job onto the job queue and get its
                         * job id. A Job Id < 0 indicates an error. See the comment
                         * above fiber_job_push in fiber.h for possible errors.
                         */
			while ((error = fbr_job_push(pool, &job)) != FBR_EOK)
				;
		}

		/* fiber_wait allows the caller to block until all jobs
                 * in the pool have finished. Be very careful when calling
                 * this if other threads or jobs can push jobs.
                 */
		error = fbr_wait(pool);
		EXIT_ERR(error);
		end = time_usec_now();

		pretty_print_result(threads_num[i], end - start);
	}

	/* This will cancel all the threads and free the pool's other
         * resources.
         */
	fbr_free(pool);
	return 0;
}

static suseconds_t time_usec_now(void)
{
	static suseconds_t us_per_sec = 1000000;
	static struct timeval now;
	gettimeofday(&now, NULL);

	return now.tv_sec * us_per_sec + now.tv_usec;
}

static struct fbr_pool *pool_init(uint32_t tnum, uint32_t queue_length)
{
	fbr_init_result_t init_res;
	fbr_init_options_t pool_options = { FBR_JQ_RING_QUEUE_OPS,
					    { malloc, free },
					    0,
					    tnum,
					    queue_length,
					    threads_num[threads_num_len - 1],
					    1,
					    true,
					    false };

	init_res = fbr_init(&pool_options);
	if (init_res.error != FBR_EOK) {
		printf("Error initializing the pool.\n");
		exit(1);
	}
	return init_res.pool;
}

static void pretty_print_result(uint32_t tnum, suseconds_t duration)
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

	printf("Time to run %u jobs with %3u threads:  %13s us\n",
	       jobs_push_num, tnum, num_start);
}
