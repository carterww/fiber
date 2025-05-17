#include <stdbool.h>
#include <stdint.h>

#include <ck_ring.h>

#include <fbr.h>
#include <fbr_errno.h>

#include "fbr_bm_alloc.h"
#include "fbr_cc.h"
#include "fbr_job.h"
#include "fbr_platform.h"

#define CAST_QUEUE_PTR(vqueue, queue) \
	struct fbr_jq_ring *queue = (struct fbr_jq_ring *)vqueue

struct fbr_jq_entries {
	struct fbr_bm_alloc_meta meta;
	struct fbr_job *array;
};

struct fbr_jq_ring {
	struct fbr_jq_entries jobs;
	char pad[FBR_CACHELINE_BYTES - sizeof(struct fbr_jq_entries)];
	struct ck_ring ck_ring;
	struct ck_ring_buffer *ck_ring_buffer;
	struct fbr_allocator allocator;
};

FBR_ATTR_PUBLIC
struct fbr_queue_init_result fbr_jq_ring_init(uint32_t cap,
					      struct fbr_allocator allocator)
{
	struct fbr_queue_init_result res = { FBR_EGENERIC, NULL };
	struct fbr_jq_ring *queue;
	size_t queue_size;
	size_t ck_ring_buffer_size;

	/* ck requires ring buffer to be a power of two and >= 4 */
	if ((cap & (cap - 1)) != 0 || cap < 4) {
		res.error = FBR_EINVAL;
	}

	queue_size = sizeof(*queue);
	ck_ring_buffer_size = (cap * sizeof(*queue->ck_ring_buffer));
	queue = allocator.malloc(queue_size + ck_ring_buffer_size);
	if (queue == NULL) {
		res.error = FBR_ENOMEM;
		goto error;
	}
	queue->jobs.array = fbr_bm_alloc_init(&queue->jobs.meta,
					      sizeof(*queue->jobs.array), cap,
					      allocator.malloc);
	if (queue->jobs.array == NULL) {
		res.error = FBR_ENOMEM;
		goto error;
	}
	ck_ring_init(&queue->ck_ring, cap);
	/* NOTE: be careful about alignment here. queue_size is a multiple of 32
	 * right now.
	 */
	queue->ck_ring_buffer =
		(struct ck_ring_buffer *)((uintptr_t)queue + queue_size);
	queue->allocator = allocator;

	res.error = FBR_EOK;
	res.queue = queue;
	return res;
error: {
	if (queue != NULL) {
		if (queue->jobs.array != NULL) {
			fbr_bm_alloc_free(&queue->jobs.meta, allocator.free);
		}
		allocator.free(queue);
	}

	res.queue = NULL;
	return res;
}
}

FBR_ATTR_PUBLIC
void fbr_jq_ring_free(void *vqueue)
{
	fbr_assert(vqueue != NULL);
	CAST_QUEUE_PTR(vqueue, queue);

	fbr_bm_alloc_free(&queue->jobs.meta, queue->allocator.free);
	queue->allocator.free(queue);
}

FBR_ATTR_PUBLIC
uint32_t fbr_jq_ring_push(void *vqueue, const struct fbr_job *job)
{
	fbr_assert(vqueue != NULL);
	fbr_assert(job != NULL);
	CAST_QUEUE_PTR(vqueue, queue);

	unsigned int entry_idx;
	fbr_errno_t malloc_err;
	struct fbr_job *local_entry;

	malloc_err = fbr_bm_malloc(&queue->jobs.meta, &entry_idx);
	if (malloc_err != FBR_EOK) {
		return 0;
	}
	local_entry = &queue->jobs.array[entry_idx];

	bool success = ck_ring_enqueue_mpmc(&queue->ck_ring,
					    queue->ck_ring_buffer, local_entry);
	if (!success) {
		fbr_bm_free(&queue->jobs.meta, entry_idx);
		return 0;
	}
	ck_pr_store_64(&local_entry->id, job->id);
	local_entry->cb = job->cb;
	local_entry->cb_arg = job->cb_arg;
	ck_pr_fence_memory();
	return 1;
}

FBR_ATTR_PUBLIC
uint32_t fbr_jq_ring_pop(void *vqueue, struct fbr_job *job_out,
			 struct fbr_job_entry *job_entry)
{
	fbr_assert(vqueue != NULL);
	fbr_assert(job_out != NULL);
	CAST_QUEUE_PTR(vqueue, queue);
	void *result;
	struct fbr_job *local_entry;
	uint32_t entry_idx;

	bool success = ck_ring_dequeue_mpmc(&queue->ck_ring,
					    queue->ck_ring_buffer, &result);
	if (!success) {
		fbr_job_entry_set_inactive(job_entry);
		return 0;
	}
	local_entry = (struct fbr_job *)result;
	entry_idx = (uint32_t)(local_entry - queue->jobs.array);
	*job_out = *local_entry;
	/* It is important that we post the job id to the job entry before
	 * marking the entry as free. This will lead to cases where the
	 * job id is still in the queue and being executed by a thread, but
	 * this is ok. All we care about is knowing if the job is in the
	 * queue OR being executed.
	 *
	 * Imagine we free the entry then set the job id. A thread that wants
	 * to wait on job X may check during the time period where the job
	 * id isn't in the queue or a job entry. This would lead to incorrect
	 * results.
	 */
	fbr_job_entry_set_active(job_entry, local_entry->id);
	ck_pr_barrier();
	fbr_bm_free(&queue->jobs.meta, entry_idx);
	return 1;
}

FBR_ATTR_PUBLIC
bool fbr_jq_ring_job_in_queue(void *vqueue, uint64_t job_id)
{
	fbr_assert(vqueue != NULL);
	CAST_QUEUE_PTR(vqueue, queue);
	uint32_t idx;
	struct fbr_bm_alloc_iterator iter;
	fbr_bm_iterator_init(&queue->jobs.meta, &iter);
	while (fbr_bm_iterator_next(&queue->jobs.meta, &iter, &idx)) {
		if (queue->jobs.array[idx].id == job_id) {
			return true;
		}
	}
	return false;
}
