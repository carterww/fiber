#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fiber.h"
#include "fiber_utils.h"
#include "job_queue.h"

// Atomic read, update, write that keeps retrying until it can atomically do all.
#define __fbr_atomic_ruw(type, to_change_ptr, prev, new_expr)                  \
	type new##type;                                                        \
	do {                                                                   \
		new##type = new_expr;                                          \
	} while (!__atomic_compare_exchange_n(to_change_ptr, &prev, new##type, \
					      0, __ATOMIC_SEQ_CST,             \
					      __ATOMIC_SEQ_CST));

#ifndef FIBER_NO_DEFAULT_QUEUE
#include "queue_impls/fifo_job_queue.h"
/* Default queue operations. Used if queue_ops are NULL in init */
static struct fiber_queue_operations def_queue_ops = {
	.push = fiber_queue_fifo_push,
	.pop = fiber_queue_fifo_pop,
	.init = fiber_queue_fifo_init,
	.free = fiber_queue_fifo_free,
	.length = fiber_queue_fifo_length,
};
#else
#warning \
	"No default queue implementation is being compiled. You MUST provide your own!"
#endif

struct pthread_arg {
	struct fiber_pool *pool;
	struct fiber_thread *self;
};

static void *__do_nothing_job(void *arg)
{
	return NULL;
}

static struct fiber_job wake_job = { .job_id = JOB_ID_MIN,
				     .job_func = __do_nothing_job,
				     .job_arg = NULL };

// fifo_job_queue.c uses these
int __fiber_mutex_init_get_err(int error);
int __fiber_sem_init_get_err(int error);
int __fiber_pthread_create_get_err(int error);

static struct fiber_queue_operations *
init_queue_ops(struct fiber_queue_operations *ops, void *(*malloc)(size_t));
static inline jid get_and_update_jid(jid *job_id_prev);
static inline jid __fiber_job_push(struct fiber_pool *pool,
				   struct fiber_job *job, uint32_t queue_flags);

/* DECLARATIONS FOR THREAD HELPER FUNCTIONS */
static inline int fiber_thread_pool_init(struct fiber_pool *pool,
					 tpsize threads_number);
static inline void fiber_thread_pool_free(struct fiber_pool *pool);
static inline int thread_ll_alloc_n(struct fiber_thread **head,
				    tpsize threads_number,
				    void *(*malloc)(size_t));
static inline void thread_ll_free(struct fiber_thread *head,
				  void (*free)(void *));
static inline void thread_ll_add(struct fiber_thread **head,
				 struct fiber_thread *new);
static inline void thread_ll_remove(struct fiber_thread **head,
				    struct fiber_thread *thread);
static int worker_threads_start(struct fiber_pool *pool,
				struct fiber_thread *head,
				tpsize threads_number);
static inline int worker_pthread_start(struct pthread_arg *arg);
static void *worker_loop(void *arg);
static inline int handle_pool_flags(struct fiber_pool *pool);
static int wake_worker_thread(struct fiber_pool *pool);
static inline void handle_flag_wait_all(struct fiber_pool *pool);
static inline void pthread_cancel_n(struct fiber_thread *head,
				    tpsize threads_number);
static inline void thread_clean_self(struct fiber_pool *pool,
				     struct fiber_thread *self);

int fiber_init(struct fiber_pool *pool, struct fiber_pool_init_options *opts)
{
	if (pool == NULL || opts == NULL) {
		return FBR_ENULL_ARGS;
	}
	if (opts->threads_number < 1 || opts->queue_length < 1) {
		return FBR_EINVLD_SIZE;
	}
	pool->malloc = opts->malloc == NULL ? malloc : opts->malloc;
	pool->free = opts->free == NULL ? free : opts->free;
	if (opts->queue_ops == NULL) {
#ifndef FIBER_NO_DEFAULT_QUEUE
		pool->queue_ops = &def_queue_ops;
#else
		return FBR_EQUEOPS_NONE;
#endif
	} else {
		if (opts->queue_ops->push == NULL ||
		    opts->queue_ops->pop == NULL ||
		    opts->queue_ops->init == NULL ||
		    opts->queue_ops->free == NULL) {
			return FBR_EQUEOPS_NONE;
		} else {
			pool->queue_ops =
				init_queue_ops(opts->queue_ops, pool->malloc);
		}
	}
	if (pool->queue_ops == NULL) {
		return FBR_EQUEOPS_NONE;
	}
	int error_code = 0;
	int mutex_res = pthread_mutex_init(&pool->lock, NULL);
	if (mutex_res != 0) {
		error_code = __fiber_mutex_init_get_err(mutex_res);
		goto err;
	}
	pool->job_id_prev = -1;
	pool->pool_flags = 0;
	if (error_code != 0) {
		goto err;
	}
	int queue_res = pool->queue_ops->init(
		&pool->job_queue, opts->queue_length, pool->malloc, pool->free);
	if (queue_res != 0) {
		error_code = queue_res;
		goto err;
	}
	if (pool->job_queue == NULL) {
		error_code = FBR_EQUE_NULL;
		goto err;
	}
	int tp_init = fiber_thread_pool_init(pool, opts->threads_number);
	if (tp_init != 0) {
		error_code = tp_init;
		goto err;
	}
	return 0;
err:
	if (mutex_res == 0) {
		int des_res = pthread_mutex_destroy(&pool->lock);
		assert(des_res == 0, "failed to destroy mutex");
	}
	if (queue_res == 0 && pool->job_queue != NULL) {
		pool->queue_ops->free(pool->job_queue);
	}
	if (pool->queue_ops != NULL) {
#ifndef FIBER_NO_DEFAULT_QUEUE
		if (pool->queue_ops != &def_queue_ops)
#endif
			pool->free((struct fiber_queue_operations *)
					   pool->queue_ops);
	}
	return error_code;
}

jid fiber_job_push(struct fiber_pool *pool, struct fiber_job *job,
		   uint32_t queue_flags)
{
	if (unlikely(pool == NULL || job == NULL || job->job_func == NULL)) {
		return FBR_ENULL_ARGS;
	}
	job->job_id = get_and_update_jid(&pool->job_id_prev);
	assert(job->job_id > -1, "given a negative job id");
	return __fiber_job_push(pool, job, queue_flags);
}

void fiber_free(struct fiber_pool *pool)
{
	if (pool == NULL || pool->queue_ops == NULL ||
	    pool->job_queue == NULL || pool->queue_ops->free == NULL ||
	    pool->free == NULL) {
		return;
	}
	pool->queue_ops->free(pool->job_queue);
	fiber_thread_pool_free(pool);
	int des_res = pthread_mutex_destroy(&pool->lock);
	assert(des_res == 0, "failed to destroy mutex");
#ifndef FIBER_NO_DEFAULT_QUEUE
	if (pool->queue_ops != &def_queue_ops)
#endif
		pool->free((struct fiber_queue_operations *)pool->queue_ops);
}

void fiber_wait(struct fiber_pool *pool)
{
	if (pool == NULL) {
		return;
	}
	__atomic_or_fetch(&pool->pool_flags, FIBER_POOL_FLAG_WAIT,
			  __ATOMIC_SEQ_CST);
	// This sequence does not cause a race condition. If the number of
	// working threads is non zero AFTER we set the pool flags, we
	// know some thread will eventaully handle it. In the case where
	// working is 0. The queue was either just empty or is empty.
	tpsize working =
		__atomic_load_n(&pool->threads_working, __ATOMIC_SEQ_CST);
	tpsize length = fiber_jobs_pending(pool);
	if (working > 0 || length > 0) {
		while (sem_wait(&pool->threads_sync) != 0 && errno == EINTR)
			;
	}
	uint32_t off = ~FIBER_POOL_FLAG_WAIT;
	__atomic_and_fetch(&pool->pool_flags, off, __ATOMIC_SEQ_CST);
}

qsize fiber_jobs_pending(struct fiber_pool *pool)
{
	if (pool == NULL || pool->job_queue == NULL ||
	    pool->queue_ops == NULL) {
		return FBR_ENULL_ARGS;
	}
	if (pool->queue_ops->length == NULL) {
		return FBR_EQUEOPS_NONE;
	}
	return pool->queue_ops->length(pool->job_queue);
}

/* THREAD CONTROL/INFO FUNCTIONS */

int fiber_threads_remove(struct fiber_pool *pool, tpsize threads_num)
{
	if (pool == NULL) {
		return FBR_ENULL_ARGS;
	}
	if (threads_num < 1) {
		return FBR_EINVLD_SIZE;
	}
	if (pool->queue_ops == NULL || pool->queue_ops->push == NULL) {
		return FBR_EPOOL_UNINIT;
	}
	// Set flag & val to notify thread it should commit seppuku
	__atomic_add_fetch(&pool->threads_kill_number, threads_num,
			   __ATOMIC_SEQ_CST);
	__atomic_or_fetch(&pool->pool_flags, FIBER_POOL_FLAG_KILL_N,
			  __ATOMIC_SEQ_CST);
	return wake_worker_thread(pool);
}

int fiber_threads_add(struct fiber_pool *pool, tpsize threads_num)
{
	if (pool == NULL) {
		return FBR_ENULL_ARGS;
	}
	if (threads_num < 1) {
		return FBR_EINVLD_SIZE;
	}
	assert(pool->threads_number + threads_num > 0, "num threads overflow");
	struct fiber_thread *threads;
	int error_code = thread_ll_alloc_n(&threads, threads_num, pool->malloc);
	if (error_code != 0) {
		return error_code;
	}
	assert(threads != NULL, "failed to alloc threads in fiber_threads_add");
	int start_res = worker_threads_start(pool, threads, threads_num);
	int lock_res = pthread_mutex_lock(&pool->lock);
	assert(lock_res == 0,
	       "Could not obtain pool lock to add threads to ll.");
	thread_ll_add(&pool->thread_head, threads);
	pthread_mutex_unlock(&pool->lock);
	if (start_res != 0) {
		return start_res;
	}
	__atomic_add_fetch(&pool->threads_number, threads_num,
			   __ATOMIC_RELAXED);
	return 0;
}

tpsize fiber_threads_number(struct fiber_pool *pool)
{
	if (pool == NULL) {
		return FBR_ENULL_ARGS;
	}
	return __atomic_load_n(&pool->threads_number, __ATOMIC_RELAXED);
}

tpsize fiber_threads_working(struct fiber_pool *pool)
{
	if (pool == NULL) {
		return FBR_ENULL_ARGS;
	}
	return __atomic_load_n(&pool->threads_working, __ATOMIC_RELAXED);
}

static jid __fiber_job_push(struct fiber_pool *pool, struct fiber_job *job,
			    uint32_t queue_flags)
{
	assert(pool->queue_ops != NULL || pool->queue_ops->push != NULL,
	       "queue_ops or push is null.");
	int push_res = pool->queue_ops->push(pool->job_queue, job, queue_flags);
	// Don't allow positive error codes to return
	if (push_res < 0) {
		return push_res;
	} else if (push_res != 0) {
		return FBR_EPUSH_JOB;
	}
	return job->job_id;
}

/* STATIC FUNCTION DEFINITIONS */

static inline jid get_and_update_jid(jid *job_id_prev)
{
	// 64 bits will probably never overflow but 32 or less may
#if JOB_ID_MAX < INT64_MAX || defined(FIBER_CHECK_JID_OVERFLOW)
	jid prev = __atomic_load_n(job_id_prev, __ATOMIC_SEQ_CST);
	__fbr_atomic_ruw(jid, job_id_prev, prev,
			 prev == JOB_ID_MAX ? -1 : prev);
#endif
	return __atomic_add_fetch(job_id_prev, 1, __ATOMIC_RELEASE);
}

static struct fiber_queue_operations *
init_queue_ops(struct fiber_queue_operations *ops, void *(*malloc)(size_t))
{
	struct fiber_queue_operations *a_ops = malloc(sizeof(*ops));
	if (a_ops == NULL) {
		return NULL;
	}
	a_ops->push = ops->push;
	a_ops->pop = ops->pop;
	a_ops->init = ops->init;
	a_ops->free = ops->free;
	a_ops->length = ops->length;
	return a_ops;
}

/* THREAD HELPER FUNCTIONS IMPLEMENTATIONS */

static int fiber_thread_pool_init(struct fiber_pool *pool,
				  tpsize threads_number)
{
	int error_code = thread_ll_alloc_n(&pool->thread_head, threads_number,
					   pool->malloc);
	if (error_code != 0) {
		goto err;
	}

	int sem_res = sem_init(&pool->threads_sync, 0, 0);
	if (sem_res != 0) {
		error_code = __fiber_sem_init_get_err(errno);
		goto err;
	}
	pool->threads_number = threads_number;
	pool->threads_working = 0;
	pool->threads_kill_number = 0;

	error_code =
		worker_threads_start(pool, pool->thread_head, threads_number);
	if (error_code != 0) {
		goto err;
	}
	return 0;
err:
	if (pool->thread_head) {
		thread_ll_free(pool->thread_head, pool->free);
	}
	if (sem_res == 0) {
		int des_res = sem_destroy(&pool->threads_sync);
		assert(des_res == 0, "failed to destroy semaphore");
	}
	return error_code;
}

static void fiber_thread_pool_free(struct fiber_pool *pool)
{
	pthread_cancel_n(pool->thread_head, THREAD_POOL_SIZE_MAX);
	thread_ll_free(pool->thread_head, pool->free);
	int des_res = sem_destroy(&pool->threads_sync);
	assert(des_res == 0, "failed to destroy semaphore");
}

static int thread_ll_alloc_n(struct fiber_thread **head, tpsize threads_number,
			     void *(*malloc)(size_t))
{
	*head = malloc(sizeof(**head));
	if (*head == NULL) {
		return errno;
	}
	struct fiber_thread *curr = *head;
	for (tpsize i = 1; i < threads_number; ++i) {
		curr->next = malloc(sizeof(*curr));
		if (curr->next == NULL) {
			return errno;
		}
		curr = curr->next;
	}
	curr->next = NULL;
	return 0;
}

static void thread_ll_free(struct fiber_thread *head, void (*free)(void *))
{
	struct fiber_thread *next;
	while (head != NULL) {
		next = head->next;
		free(head);
		head = next;
	}
}

static void thread_ll_add(struct fiber_thread **head, struct fiber_thread *new)
{
	assert(new != NULL, "tried to add NULL to thread ll");
	if (*head == NULL) {
		*head = new;
		return;
	}
	struct fiber_thread *next = (*head)->next;
	(*head)->next = new;
	while (new->next != NULL) {
		new = new->next;
	}
	new->next = next;
}

static void thread_ll_remove(struct fiber_thread **head,
			     struct fiber_thread *thread)
{
	assert(*head != NULL, "No threads in ll.");
	assert(thread != NULL, "Tried to remove NULL from thread ll.");
	struct fiber_thread *curr = (*head)->next;
	if (*head == thread) {
		*head = curr;
		return;
	}
	struct fiber_thread *prev = *head;
	while (curr != NULL) {
		if (curr != thread) {
			prev = curr;
			curr = curr->next;
			continue;
		}
		prev->next = curr->next;
		return;
	}
}

// In case of an error, we need to save previously allocated
// args. This allows us to walk backward and free the args.
struct pthread_arg_ll {
	struct pthread_arg arg;
	struct pthread_arg_ll *prev;
};
static int worker_threads_start(struct fiber_pool *pool,
				struct fiber_thread *head,
				tpsize threads_number)
{
	int error_code = 0;
	tpsize i = 0;
	struct pthread_arg_ll *prev = NULL;
	while (i < threads_number && head != NULL) {
		struct pthread_arg_ll *arg_link =
			pool->malloc(sizeof(*arg_link));
		if (arg_link == NULL) {
			error_code = ENOMEM;
			goto err;
		}
		arg_link->prev = prev;
		arg_link->arg.pool = pool;
		arg_link->arg.self = head;
		arg_link->arg.self->job_id = -1;
		prev = arg_link;
		error_code = worker_pthread_start(&arg_link->arg);
		if (error_code != 0) {
			goto err;
		}
		++i;
		head = head->next;
	}

	return 0;
err:
	if (i > 0) {
		pthread_cancel_n(head, i - 1);
	}
	while (prev != NULL) {
		struct pthread_arg_ll *saved = prev->prev;
		pool->free(prev);
		prev = saved;
	}
	return error_code;
}

static int worker_pthread_start(struct pthread_arg *arg)
{
	assert(arg != NULL, "Tried to start pthraed with NULL arg");
	int error_code =
		pthread_create(&arg->self->thread_id, NULL, worker_loop, arg);
	if (error_code != 0) {
		return __fiber_pthread_create_get_err(error_code);
	}
	return pthread_detach(arg->self->thread_id);
}

static void *worker_loop(void *arg)
{
	struct pthread_arg *kit = (struct pthread_arg *)arg;
	struct fiber_pool *pool = kit->pool;
	struct fiber_thread *self = kit->self;
	assert(pool != NULL, "worker_loop passed NULL fiber_pool");
	assert(self != NULL, "worker_loop passed NULL fiber_thread");
	int (*job_pop)(void *, struct fiber_job *, uint32_t) =
		pool->queue_ops->pop;
	struct fiber_job job_buf = { 0 };

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	int last_handle_flags_res = 0;
	while (1) {
		__atomic_store_n(&self->job_id, -1, __ATOMIC_RELAXED);
		int pop_res = job_pop(pool->job_queue, &job_buf, FIBER_BLOCK);
		if (pop_res != 0) {
			sched_yield();
			continue;
		}

		__atomic_add_fetch(&pool->threads_working, 1, __ATOMIC_RELAXED);
		do {
			__atomic_store_n(&self->job_id, job_buf.job_id,
					 __ATOMIC_RELAXED);
			job_buf.job_func(job_buf.job_arg);
			// Should this be atomic load? I don't think it matters
			if (pool->pool_flags & FIBER_POOL_FLAG_KILL_N) {
				break; // Break queue pop loop
			}
		} while (job_pop(pool->job_queue, &job_buf, 0) == 0);
		__atomic_sub_fetch(&pool->threads_working, 1, __ATOMIC_RELAXED);

		if ((last_handle_flags_res = handle_pool_flags(pool)) != 0) {
			break; // Break while(1) loop
		}
	} // End of while(1) loop
	assert(last_handle_flags_res != 0,
	       "A thread reached its cleanup without being told to by handle_pool_flags");
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	thread_clean_self(pool, self);
	if (kit) {
		pool->free(kit);
	}
	pthread_exit(0);
}

static int handle_pool_flags(struct fiber_pool *pool)
{
	uint32_t pool_flags =
		__atomic_load_n(&pool->pool_flags, __ATOMIC_SEQ_CST);
	if (pool_flags & FIBER_POOL_FLAG_KILL_N) {
		tpsize to_kill = __atomic_sub_fetch(&pool->threads_kill_number,
						    1, __ATOMIC_SEQ_CST);
		if (to_kill > 0) {
			int wake_res = wake_worker_thread(pool);
			assert(wake_res == 0, "wake_worker_thread failed");
			return 1; // Non zero value to indicate we should exit
		} else {
			uint32_t off = ~FIBER_POOL_FLAG_KILL_N;
			__atomic_and_fetch(&pool->pool_flags, off,
					   __ATOMIC_SEQ_CST);
			return 1;
		}
	}
	if (pool_flags & FIBER_POOL_FLAG_WAIT) {
		handle_flag_wait_all(pool);
	}
	return 0;
}

static int wake_worker_thread(struct fiber_pool *pool)
{
	assert(pool != NULL, "caller did not check pool for NULL");
	// Put a job onto the queue whose sole purpose is to wake up
	// a thread and allow it to handle the flags we just set.
	int res = __fiber_job_push(pool, &wake_job, FIBER_BLOCK);
	assert(res != FBR_ENULL_ARGS,
	       "we passed a null job or job_func. check wake_job global");
	return res < 0 ? res : 0;
}

static void handle_flag_wait_all(struct fiber_pool *pool)
{
	tpsize tworking =
		__atomic_load_n(&pool->threads_working, __ATOMIC_RELAXED);
	if (tworking > 0) {
		return;
	}
	int res = sem_post(&pool->threads_sync);
	assert(res == 0, "sem_post error. probably overflow");
}

static void pthread_cancel_n(struct fiber_thread *head, tpsize threads_number)
{
	tpsize i = 0;
	while (head != NULL && i < threads_number) {
		int res = pthread_cancel(head->thread_id);
		assert(res == 0, "pthread_cancel returned an error");
		head = head->next;
		++i;
	}
}

static inline void thread_clean_self(struct fiber_pool *pool,
				     struct fiber_thread *self)
{
	int lock_res = pthread_mutex_lock(&pool->lock);
	assert(lock_res == 0,
	       "Could not obtain pool lock to remove thread from ll.");
	thread_ll_remove(&pool->thread_head, self);
	pool->free(self);
	pthread_mutex_unlock(&pool->lock);
	__atomic_fetch_sub(&pool->threads_number, 1, __ATOMIC_RELAXED);
}

/* INTERNAL MISC FUNCTIONS */

static const char *invalid_error_msg = "__*_get_err cannot take 0\n";

int __fiber_mutex_init_get_err(int error)
{
	assert(error != 0, invalid_error_msg);
	switch (error) {
	case EAGAIN: // System did not have resource to init mutx (excluding mem).
		return FBR_ENO_RSC;
	case EPERM: // Does not have permission to init mutex
		return FBR_EPTHRD_PERM;
	case ENOMEM: // Not enough mem
	case EBUSY: // Mutex already initialized
	case EINVAL: // Attr invalid
	default:
		return error;
	}
}

int __fiber_sem_init_get_err(int error)
{
	assert(error != 0, invalid_error_msg);
	switch (error) {
	case EINVAL: // Semaphore value too large
		return FBR_ESEM_RNG;
	case ENOSYS: // Don't use pshared, should not encounter.
	default:
		return error;
	}
}

int __fiber_pthread_create_get_err(int error)
{
	assert(error != 0, invalid_error_msg);
	switch (error) {
	case EAGAIN: // Insufficent resources to create another thread
		return FBR_ENO_RSC;
	case EPERM: // Permissions erro
		return FBR_EPTHRD_PERM;
	case EINVAL: // Invalid attr
	default:
		return error;
	}
}
