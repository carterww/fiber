#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <fbr.h>
#include <fbr_jq_ring.h>

int main(void)
{
	fbr_init_options_t pool_options = {
		FBR_JQ_RING_QUEUE_OPS, { malloc, free }, 8, 512, 10, 64,
	};
	fbr_init_result_t init_res = fbr_init(&pool_options);
	if (init_res.error != FBR_EOK) {
		return 1;
	}
	uint32_t started = 8;
	/* This will only start 2 */
	fbr_thread_add(init_res.pool, &started);
	fbr_thread_remove(init_res.pool, 5);
	sleep(1);
	fbr_free(init_res.pool);
	sleep(2);
	return 0;
}
