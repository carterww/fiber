/* See LICENSE file for copyright and license details. */

#include <stdio.h>
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
	unsigned long a = 0, b = 1;
	int i = 1;
	// I know b isn't the real fib due to overflow
	// I just needed busy work
	while (i < fib_to) {
		unsigned long c = a + b;
		a = b;
		b = c;
		++i;
	}
	return (void *)b;
}

int main(int argc, char *argv[])
{
	struct fiber_pool *pool = NULL;
	{
		// Use the fifo queue implementation
		struct fiber_queue_operations queue_ops =
			FIBER_FIFO_QUEUE_OPERATIONS;
		struct fiber_pool_init_options pool_opts = {
			.queue_ops = &queue_ops,
			.malloc = NULL, // Use libc malloc
			.free = NULL, // Use libc free
			.queue_length = queue_len,
			.threads_number = threads_num,
		};
		struct fiber_init_result init_res = fiber_init(&pool_opts);
		if (init_res.error != 0) {
			return 1;
		}
		pool = init_res.pool;
	}
	int i = 0;
	while (i < jobs_num) {
		struct fiber_job job = {
			.job_func = runner,
			.job_arg = NULL,
		};
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
