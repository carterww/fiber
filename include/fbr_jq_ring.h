/* See LICENSE file for copyright and license details. */

#ifndef FBR_JQ_RING_H
#define FBR_JQ_RING_H

#include <stdint.h>

#include <fbr.h>

struct fbr_queue_init_result fbr_jq_ring_init(uint32_t cap, void *buffer,
					      size_t buffer_size,
					      struct fbr_allocator allocator);
void fbr_jq_ring_free(void *vqueue);
uint32_t fbr_jq_ring_push(void *vqueue, const struct fbr_job *job);
uint32_t fbr_jq_ring_pop(void *vqueue, struct fbr_job *job_out,
			 struct fbr_job_entry *job_entry);
bool fbr_jq_ring_job_in_queue(void *vqueue, uint64_t job_id);
size_t fbr_jq_ring_size_required(uint32_t queue_len);

#define FBR_JQ_RING_QUEUE_OPS                                        \
	{                                                            \
		fbr_jq_ring_push,	  fbr_jq_ring_pop,           \
		fbr_jq_ring_init,	  fbr_jq_ring_free,          \
		fbr_jq_ring_job_in_queue, fbr_jq_ring_size_required, \
	}

#endif /* FBR_JQ_RING_H */
