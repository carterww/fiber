#include <unistd.h>

#include "fiber.h"
#include "job_queue.h"

struct fiber_pool pool = { 0 };

void *runner(void *arg)
{
	static const char *msg = "Hello from runner!\n";
	write(2, msg, 19);
	return NULL;
}

int main()
{
	struct fiber_pool_init_options pool_opts = {
		.queue_ops = NULL, // Use default FIFO
		.queue_length = 500,
		.threads_number = 4,
	};
	int init_res = fiber_init(&pool, &pool_opts);
	if (init_res != 0) {
		return 1;
	}
	int i = 0;
	while (i < 500) {
		struct fiber_job job = {
			.job_func = runner,
			.job_arg = NULL,
		};
		jid job_id = fiber_job_push(&pool, &job, FIBER_BLOCK);
		if (job_id == -1) {
			return 1;
		}
	}
	fiber_wait(&pool);
	return 0;
}
