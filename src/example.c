/* See LICENSE file for copyright and license details. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fiber.h"
#include "fiber_fifo.h"

static int fib_to = 100000;
static int jobs_num = 500000;
static int queue_len = 1000;
static int threads_num = 2;

void *runner(void *arg)
{
	unsigned long a = 0;
	unsigned long b = 1;
	int i = 1;

	(void)arg;
	/* I know b isn't the real fib due to overflow
	 * I just needed busy work
         */
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
	int i = 0;
	struct fiber_pool *pool = NULL;
	{
		struct fiber_queue_operations queue_ops =
			FIBER_FIFO_QUEUE_OPERATIONS;
		struct fiber_pool_init_options pool_opts;
		struct fiber_init_result init_res;

		/* Use the fifo queue implementation */
		pool_opts.queue_ops = &queue_ops;
		pool_opts.malloc = malloc; /* Use libc malloc */
		pool_opts.free = free; /* Use libc free */
		pool_opts.queue_length = queue_len;
		pool_opts.threads_number = threads_num;

		init_res = fiber_init(&pool_opts);
		if (init_res.error != 0) {
			return 1;
		}
		pool = init_res.pool;
	}
	if (!fiber_libversion_compatible()) {
		const struct fiber_version libversion = fiber_libversion();
		printf("The header file version you are using is not compatible with the library's.\n");
		printf("Header file version: %d.%d.%d\n", FIBER_VERSION_MAJOR,
		       FIBER_VERSION_MINOR, FIBER_VERSION_PATCH);
		printf("Library's version:   %d.%d.%d\n", libversion.major,
		       libversion.minor, libversion.patch);
		return 1;
	}
	while (i < jobs_num) {
		struct fiber_job job = { -1, runner, NULL };
		jid job_id = fiber_job_push(pool, &job, FIBER_QUEUE_BLOCK);
		if (job_id < 0) {
			return 1;
		}
		++i;
	}
	fiber_wait(pool);
	fiber_free(pool);
	return 0;
}
