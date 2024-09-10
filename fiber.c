#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fiber.h"
#include "job_queue.h"

#ifdef FIBER_ASSERTS
#define assert(expr, err_msg)                                                 \
	fprintf(STDERR_FILENO, "ERR: assert failed at %s:%d -> %s", __FILE__, \
		__LINE__, err_msg);
#else
#define assert(expr, err_msg)
#endif

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
	.capactity = fiber_queue_fifo_capacity,
	.length = fiber_queue_fifo_length,
};
#endif

struct pthread_arg {
	struct fiber_pool *pool;
	struct fiber_thread *self;
};

static void sigusr1_handler(int signum)
{
}

// fifo_job_queue.c uses these
void __fiber_die(const char *msg, int fd, int exit_code);
int __fiber_mutex_init_get_err(int error);
int __fiber_sem_init_get_err(int error);
int __fiber_pthread_create_get_err(int error);

static struct fiber_queue_operations *
init_queue_ops(struct fiber_queue_operations *ops, void *(*malloc)(size_t));
static inline jid get_and_update_jid(struct fiber_pool *pool);

/* DECLARATIONS FOR THREAD HELPER FUNCTIONS */
static int fiber_thread_pool_init(struct fiber_pool *pool,
				  tpsize threads_number);
static void fiber_thread_pool_free(struct fiber_pool *pool);
static int thread_ll_alloc_n(struct fiber_pool *pool,
			     struct fiber_thread **head, tpsize threads_number);
static void thread_ll_free(struct fiber_pool *pool, struct fiber_thread *head);
static void thread_ll_add(struct fiber_pool *pool, struct fiber_thread *head);
static int thread_ll_remove(struct fiber_pool *pool,
			    struct fiber_thread *thread);
static int worker_threads_start(struct fiber_pool *pool,
				struct fiber_thread *head,
				tpsize threads_number);
static int worker_pthread_start(struct pthread_arg *arg);
static void *worker_loop(void *arg);
static int worker_register_signal_handlers();
static void handle_flag_wait_all(struct fiber_pool *pool);
static void wake_next_sleeping_thread(struct fiber_pool *pool);
static void pthread_cancel_n(struct fiber_thread *head, tpsize threads_number);
static void thread_clean_self(struct fiber_pool *pool,
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
	if (mutex_res == 0)
		pthread_mutex_destroy(&pool->lock);
	if (queue_res == 0 && pool->job_queue != NULL)
		pool->queue_ops->free(pool->job_queue);
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
	if (pool == NULL || job == NULL || job->job_func == NULL) {
		return FBR_ENULL_ARGS;
	}
	job->job_id = get_and_update_jid(pool);
	assert(pool->queue_ops != NULL || pool->queue_ops->push != NULL,
	       "queue_ops or push is null.");
	int push_res = pool->queue_ops->push(pool->job_queue, job, queue_flags);
	if (push_res != 0) {
		return FBR_EPUSH_JOB;
	}
	return job->job_id;
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
	pthread_mutex_destroy(&pool->lock);
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
	while (sem_wait(&pool->threads_sync) != 0 && errno == EINTR)
		;
	uint32_t off = ~FIBER_POOL_FLAG_WAIT;
	__atomic_and_fetch(&pool->pool_flags, off, __ATOMIC_SEQ_CST);
}

qsize fiber_jobs_pending(struct fiber_pool *pool)
{
	if (pool == NULL || pool->job_queue == NULL ||
	    pool->queue_ops == NULL || pool->queue_ops->length == NULL) {
		return -1;
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
	// Set flag to notify thread it should commit seppuku
	__atomic_add_fetch(&pool->threads_kill_number, threads_num,
			   __ATOMIC_SEQ_CST);
	__atomic_or_fetch(&pool->pool_flags, FIBER_POOL_FLAG_KILL_N,
			  __ATOMIC_SEQ_CST);
	wake_next_sleeping_thread(pool);
	return 0;
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
	int error_code = thread_ll_alloc_n(pool, &threads, threads_num);
	if (error_code != 0) {
		return error_code;
	}
	int start_res = worker_threads_start(pool, threads, threads_num);
	if (start_res != 0) {
		return start_res;
	}
	thread_ll_add(pool, threads);
	__atomic_add_fetch(&pool->threads_number, threads_num,
			   __ATOMIC_RELAXED);
	return 0;
}

tpsize fiber_threads_number(struct fiber_pool *pool)
{
	if (pool == NULL) {
		return 0;
	}
	return __atomic_load_n(&pool->threads_number, __ATOMIC_RELAXED);
}

tpsize fiber_threads_working(struct fiber_pool *pool)
{
	if (pool == NULL) {
		return THREAD_POOL_SIZE_MAX;
	}
	return __atomic_load_n(&pool->threads_working, __ATOMIC_RELAXED);
}

/* STATIC FUNCTION DEFINITIONS */

static inline jid get_and_update_jid(struct fiber_pool *pool)
{
	// 64 bits will probably never overflow but 32 or less may
#if JOB_ID_MAX < INT64_MAX || defined(FIBER_CHECK_JID_OVERFLOW)
	jid prev = __atomic_load_n(&pool->job_id_prev, __ATOMIC_SEQ_CST);
	__fbr_atomic_ruw(jid, &pool->job_id_prev, prev,
			 prev == JOB_ID_MAX ? -1 : prev + 1);
#endif
	return __atomic_add_fetch(&pool->job_id_prev, 1, __ATOMIC_RELEASE);
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
	a_ops->capactity = ops->capactity;
	a_ops->length = ops->length;
	return a_ops;
}

/* THREAD HELPER FUNCTIONS IMPLEMENTATIONS */

static int fiber_thread_pool_init(struct fiber_pool *pool,
				  tpsize threads_number)
{
	int error_code =
		thread_ll_alloc_n(pool, &pool->thread_head, threads_number);
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
	if (pool->thread_head)
		thread_ll_free(pool, pool->thread_head);
	if (sem_res == 0)
		sem_destroy(&pool->threads_sync);
	return error_code;
}

static void fiber_thread_pool_free(struct fiber_pool *pool)
{
	pthread_mutex_lock(&pool->lock);
	pthread_cancel_n(pool->thread_head, THREAD_POOL_SIZE_MAX);
	thread_ll_free(pool, pool->thread_head);
	sem_destroy(&pool->threads_sync);
	pthread_mutex_unlock(&pool->lock);
}

static int thread_ll_alloc_n(struct fiber_pool *pool,
			     struct fiber_thread **head, tpsize threads_number)
{
	*head = pool->malloc(sizeof(**head));
	if (*head == NULL) {
		return errno;
	}
	struct fiber_thread *curr = *head;
	for (tpsize i = 1; i < threads_number; ++i) {
		curr->next = pool->malloc(sizeof(*curr));
		if (curr->next == NULL) {
			return errno;
		}
		curr = curr->next;
	}
	curr->next = NULL;
	return 0;
}

static void thread_ll_free(struct fiber_pool *pool, struct fiber_thread *head)
{
	struct fiber_thread *curr = head;
	struct fiber_thread *next;
	while (curr != NULL) {
		next = curr->next;
		pool->free(curr);
		curr = next;
	}
}

static inline void thread_ll_add(struct fiber_pool *pool,
				 struct fiber_thread *head)
{
	int lock_res = pthread_mutex_lock(&pool->lock);
	if (lock_res != 0 && lock_res != EDEADLK) {
		// Log to stderr?
		return;
	}
	if (pool->thread_head == NULL) {
		pool->thread_head = head;
		goto unlock;
	}
	struct fiber_thread *phead_next = pool->thread_head->next;
	pool->thread_head->next = head;
	head->next = phead_next;
unlock:
	pthread_mutex_unlock(&pool->lock);
}

static inline int thread_ll_remove(struct fiber_pool *pool,
				   struct fiber_thread *thread)
{
	int res = -1;
	if (pool->thread_head == NULL) {
		return res;
	}
	int lock_res = pthread_mutex_lock(&pool->lock);
	if (lock_res != 0 && lock_res != EDEADLK) {
		// Log to stderr?
		return res;
	}
	if (pool->thread_head == thread) {
		pool->thread_head = pool->thread_head->next;
		res = 0;
		goto unlock;
	}
	struct fiber_thread *prev = pool->thread_head;
	struct fiber_thread *curr = prev->next;
	while (curr != NULL) {
		if (curr != thread) {
			prev = curr;
			curr = curr->next;
			continue;
		}
		prev->next = curr->next;
		res = 0;
		break;
	}
unlock:
	pthread_mutex_unlock(&pool->lock);
	return res;
}

static int worker_threads_start(struct fiber_pool *pool,
				struct fiber_thread *head,
				tpsize threads_number)
{
	struct pthread_arg *args = pool->malloc(threads_number * sizeof(*args));
	if (args == NULL) {
		return errno;
	}
	int error_code = 0;
	tpsize i = 0;
	while (i < threads_number && head != NULL) {
		struct pthread_arg *arg = &args[i];
		arg->pool = pool;
		arg->self = head;
		arg->self->job_id = -1;
		error_code = worker_pthread_start(arg);
		if (error_code != 0) {
			goto err;
		}
		++i;
		head = head->next;
	}

	return 0;
err:
	if (i > 0)
		pthread_cancel_n(head, i - 1);
	return error_code;
}

static int worker_pthread_start(struct pthread_arg *arg)
{
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
	int (*job_pop)(void *, struct fiber_job *, uint32_t) =
		pool->queue_ops->pop;
	struct fiber_job job_buf = { 0 };

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	worker_register_signal_handlers();

	while (1) {
		int pop_res = job_pop(pool->job_queue, &job_buf, FIBER_BLOCK);
		if (pop_res == EINTR) {
			goto handle_flags;
		} else if (pop_res != 0) {
			sched_yield();
			continue;
		}
		__atomic_fetch_add(&pool->threads_working, 1, __ATOMIC_RELAXED);

		do {
			__atomic_store_n(&self->job_id, job_buf.job_id,
					 __ATOMIC_RELAXED);
			job_buf.job_func(job_buf.job_arg);
			// Should this be atomic load? I don't think it matters
			if (pool->pool_flags & FIBER_POOL_FLAG_KILL_N) {
				goto handle_flags;
			}
		} while (job_pop(pool->job_queue, &job_buf, 0) == 0);
		__atomic_store_n(&self->job_id, -1, __ATOMIC_RELAXED);

		__atomic_fetch_sub(&pool->threads_working, 1, __ATOMIC_RELAXED);
handle_flags: {
	uint32_t pool_flags =
		__atomic_load_n(&pool->pool_flags, __ATOMIC_SEQ_CST);
	if (pool_flags & FIBER_POOL_FLAG_KILL_N) {
		tpsize to_kill = __atomic_fetch_sub(&pool->threads_kill_number,
						    1, __ATOMIC_SEQ_CST);
		if (to_kill > 0) {
			wake_next_sleeping_thread(pool);
			goto exit_thread;
		} else {
			uint32_t off = ~FIBER_POOL_FLAG_KILL_N;
			__atomic_fetch_and(&pool->pool_flags, off,
					   __ATOMIC_SEQ_CST);
		}
	}
	if (pool_flags & FIBER_POOL_FLAG_WAIT) {
		handle_flag_wait_all(pool);
	}
}
	}

exit_thread:
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	thread_clean_self(pool, self);
	if (kit)
		pool->free(kit);
	pthread_exit(0);
}

static inline int worker_register_signal_handlers()
{
	struct sigaction sa;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = sigusr1_handler;
	if (sigaction(SIGUSR1, &sa, NULL) == -1) {
		return errno;
	}
	return 0;
}

static void handle_flag_wait_all(struct fiber_pool *pool)
{
	tpsize tworking =
		__atomic_load_n(&pool->threads_working, __ATOMIC_RELAXED);
	if (tworking > 0) {
		return;
	}
	if (sem_post(&pool->threads_sync) != 0) {
		__fiber_die("sem_post error. probably overflow\n",
			    STDERR_FILENO, 1);
		return;
	}
}

static void wake_next_sleeping_thread(struct fiber_pool *pool)
{
	int lock_res = pthread_mutex_lock(&pool->lock);
	if (lock_res != 0) {
		return;
	}
	struct fiber_thread *head = pool->thread_head;
	while (head != NULL) {
		jid curr_job_id =
			__atomic_load_n(&head->job_id, __ATOMIC_SEQ_CST);
		if (curr_job_id < 0) {
			// Wake up thread to handle new flags
			pthread_kill(head->thread_id, SIGUSR1);
			break;
		}
		head = head->next;
	}
	pthread_mutex_unlock(&pool->lock);
}

static void pthread_cancel_n(struct fiber_thread *head, tpsize threads_number)
{
	tpsize i = 0;
	while (head != NULL && i < threads_number) {
		pthread_cancel(head[i++].thread_id);
	}
}

static inline void thread_clean_self(struct fiber_pool *pool,
				     struct fiber_thread *self)
{
	int found = thread_ll_remove(pool, self);
	if (found == 0) {
		pool->free(self);
	}
	__atomic_fetch_sub(&pool->threads_number, 1, __ATOMIC_RELAXED);
}

/* INTERNAL MISC FUNCTIONS */

void __fiber_die(const char *msg, int fd, int exit_code)
{
	if (fd < 1)
		fd = STDERR_FILENO;
	unsigned long len = strlen(msg);
	write(fd, msg, len);
	exit(exit_code);
}

static const char *invalid_error_msg = "__*_get_err cannot take 0\n";

int __fiber_mutex_init_get_err(int error)
{
	switch (error) {
	case 0:
		__fiber_die(invalid_error_msg, STDERR_FILENO, 1);
		return 0;
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
	switch (error) {
	case 0:
		__fiber_die(invalid_error_msg, STDERR_FILENO, 1);
		return 0;
	case EINVAL: // Semaphore value too large
		return FBR_ESEM_RNG;
	case ENOSYS: // Don't use pshared, should not encounter.
	default:
		return error;
	}
}

int __fiber_pthread_create_get_err(int error)
{
	switch (error) {
	case 0:
		__fiber_die(invalid_error_msg, STDERR_FILENO, 1);
		return 0;
	case EAGAIN: // Insufficent resources to create another thread
		return FBR_ENO_RSC;
	case EPERM: // Permissions erro
		return FBR_EPTHRD_PERM;
	case EINVAL: // Invalid attr
	default:
		return error;
	}
}

#undef assert
