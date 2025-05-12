#ifndef FBR_JQ_RING_H
#define FBR_JQ_RING_H

#include <stdint.h>

#include <fbr.h>

struct fbr_queue_init_result fbr_jq_ring_init(uint32_t cap,
					      struct fbr_allocator allocator);
void fbr_jq_ring_free(void *vqueue);
uint32_t fbr_jq_ring_push(void *vqueue, const struct fbr_job *job);
uint32_t fbr_jq_ring_pop(void *vqueue, struct fbr_job *job_out);

#define FBR_JQ_RING_QUEUE_OPS     \
	{                         \
		fbr_jq_ring_push, \
		fbr_jq_ring_pop,  \
		fbr_jq_ring_init, \
		fbr_jq_ring_free, \
	}

#endif /* FBR_JQ_RING_H */
