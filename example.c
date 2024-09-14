/* See LICENSE file for copyright and license details. */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "fiber.h"
#include "job_queue.h"

struct fiber_pool pool = { 0 };

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
	struct fiber_pool_init_options pool_opts = {
		.queue_ops = NULL, // Use default FIFO
		.queue_length = queue_len,
		.threads_number = threads_num,
	};
	int init_res = fiber_init(&pool, &pool_opts);
	if (init_res != 0) {
		return 1;
	}
	int i = 0;
	while (i < jobs_num) {
		struct fiber_job job = {
			.job_func = runner,
			.job_arg = NULL,
		};
		jid job_id = fiber_job_push(&pool, &job, FIBER_BLOCK);
		if (job_id < 0) {
			return 1;
		}
		++i;
	}
	fiber_wait(&pool);
	return 0;
}
