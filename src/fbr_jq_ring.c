#include "fbr_debug.h"
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
	struct ck_ring ck_ring;
	struct ck_ring_buffer *ck_ring_buffer;
};

inline static size_t fbr_jq_ring_size_base(void)
{
	return FBR_SIZE_ROUND_CACHELINE(sizeof(struct fbr_jq_ring));
}

inline static size_t fbr_jq_ring_size_ck_ring_buffer(uint32_t cap)
{
	size_t ck_ring_buffer_size;
	ck_ring_buffer_size = (cap * sizeof(struct ck_ring_buffer));
	ck_ring_buffer_size = FBR_SIZE_ROUND_CACHELINE(ck_ring_buffer_size);
	return ck_ring_buffer_size;
}

inline static size_t fbr_jq_ring_size_bm(uint32_t cap)
{
	size_t bm_size;
	bm_size = fbr_bm_alloc_size(cap, sizeof(struct fbr_job));
	return bm_size;
}

FBR_ATTR_PUBLIC
struct fbr_queue_init_result fbr_jq_ring_init(uint32_t cap, void *buffer,
					      size_t buffer_size,
					      struct fbr_allocator allocator)
{
	struct fbr_queue_init_result res = { FBR_EGENERIC, NULL };
	struct fbr_jq_ring *queue;
	uintptr_t buffer_current;
	size_t queue_size;
	size_t ck_ring_buffer_size;
	size_t bm_size;

	(void)allocator;

	/* ck requires ring buffer to be a power of two and >= 4 */
	if ((cap & (cap - 1)) != 0 || cap < 4) {
		res.error = FBR_EINVAL;
		return res;
	}

	queue_size = fbr_jq_ring_size_base();
	ck_ring_buffer_size = fbr_jq_ring_size_ck_ring_buffer(cap);
	bm_size = fbr_jq_ring_size_bm(cap);

	if (buffer_size < queue_size + ck_ring_buffer_size + bm_size) {
		res.error = FBR_EINVLD_SIZE;
		return res;
	}
	if (!fbr_aligned(buffer, FBR_ALIGNMENT_MIN)) {
		res.error = FBR_EINVAL;
		return res;
	}
	queue = buffer;
	buffer_current = (uintptr_t)buffer + queue_size;
	queue->ck_ring_buffer = (void *)buffer_current;
	fbr_assert(fbr_aligned(queue->ck_ring_buffer, FBR_ALIGNMENT_MIN));
	buffer_current += ck_ring_buffer_size;

	queue->jobs.array = fbr_bm_alloc_init(&queue->jobs.meta,
					      sizeof(*queue->jobs.array), cap,
					      (void *)buffer_current, bm_size);
	fbr_assert(queue->jobs.array != NULL);
	fbr_assert(fbr_aligned(queue->jobs.array, FBR_ALIGNMENT_MIN));

	ck_ring_init(&queue->ck_ring, cap);
	res.error = FBR_EOK;
	res.queue = queue;
	return res;
}

FBR_ATTR_PUBLIC
void fbr_jq_ring_free(void *vqueue)
{
	fbr_assert(vqueue != NULL);
	(void)vqueue;
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

FBR_ATTR_PUBLIC
size_t fbr_jq_ring_size_required(uint32_t cap)
{
	return fbr_jq_ring_size_base() + fbr_jq_ring_size_ck_ring_buffer(cap) +
	       fbr_jq_ring_size_bm(cap);
}
