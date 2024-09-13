#include <errno.h>

#include "fiber.h"
#include "fiber_utils.h"
#include <pthread.h>
#include <semaphore.h>
#include "penrose_job_queue.h"

// From fiber.c
extern int __fiber_mutex_init_get_err(int error);
extern int __fiber_sem_init_get_err(int error);

int fiber_queue_penrose_init(void **queue, qsize capacity,
			     void *(*malloc)(size_t), void (*free)(void *))
{
	assert(queue != NULL, "penrose_init received a NULL queue");
	assert(capacity > 0, "penrose_init received a bad capacity");
	assert(malloc != NULL, "penrose_init received a NULL malloc func");
	assert(free != NULL, "penrose_init received a NULL malloc func");
	if (capacity > UINT16_MAX) {
		return FBR_EINVLD_SIZE;
	}
	int error_code = 0;
	struct penrose_jq *pq = malloc(sizeof(*pq) + PENROSE_EXTRA_ALLOC);
	if (pq == NULL) {
		error_code = ENOMEM;
		goto err;
	}
	struct penrose_stair *push =
		malloc(sizeof(*push) + PENROSE_EXTRA_ALLOC);
	if (push == NULL) {
		error_code = ENOMEM;
		goto err;
	}
	struct penrose_stair *pop = malloc(sizeof(*pop));
	if (pop == NULL) {
		error_code = ENOMEM;
		goto err;
	}
	int sem_jobs_res = sem_init(&pq->jobs_num_total, 0, 0);
	if (sem_jobs_res != 0) {
		error_code = __fiber_sem_init_get_err(errno);
		goto err;
	}
	int push_mutex_res = pthread_mutex_init(&pq->stair_push_lock, NULL);
	if (push_mutex_res != 0) {
		error_code = __fiber_mutex_init_get_err(push_mutex_res);
		goto err;
	}
	int pop_mutex_res = pthread_mutex_init(&pq->stair_pop_lock, NULL);
	if (pop_mutex_res != 0) {
		error_code = __fiber_mutex_init_get_err(pop_mutex_res);
		goto err;
	}

	pq->stair_push = push;
	pq->stair_pop = pop;
	pq->stair_jobs_capacity = penrose_page_job_capacity();
	pq->malloc = malloc;
	pq->free = free;
	*queue = pq;
	return 0;
err:
	if (push_mutex_res == 0)
		pthread_mutex_destroy(&pq->stair_push_lock);
	if (pop_mutex_res == 0)
		pthread_mutex_destroy(&pq->stair_pop_lock);
	if (sem_jobs_res == 0)
		sem_destroy(&pq->jobs_num_total);
	if (pop != NULL)
		free(pop);
	if (push != NULL)
		free(push);
	if (pq != NULL)
		free(pq);
	return error_code;
}

int fiber_queue_penrose_push(void *queue, struct fiber_job *job,
			     uint32_t flags);

int fiber_queue_penrose_pop(void *queue, struct fiber_job *buffer,
			    uint32_t flags);

void fiber_queue_penrose_free(void *queue)
{
	assert(queue != NULL, "penrose_free given a NULL queue");
	struct penrose_jq *pq = (struct penrose_jq *)queue;
	sem_destroy(&pq->jobs_num_total);
	pthread_mutex_destroy(&pq->stair_push_lock);
	pthread_mutex_destroy(&pq->stair_pop_lock);
	struct penrose_stair *curr = pq->stair_pop;
	struct penrose_stair *next;
	while (curr != pq->stair_push) {
		next = curr->next;
		pq->free(curr);
		curr = next;
	}
	pq->free(pq->stair_push);
}

qsize fiber_queue_penrose_length(void *queue)
{
	assert(queue != NULL, "penrose_length given NULL queue");
	struct penrose_jq *pq = (struct penrose_jq *)queue;
	int sem_val;
	int error_code = sem_getvalue(&pq->jobs_num_total, &sem_val);
	if (error_code != 0 || sem_val < 0) {
		return 0;
	}
	return sem_val;
}
