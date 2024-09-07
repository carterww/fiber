#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>

#include "fiber.h"
#include "pool.h"

struct __pthread_arg {
  struct fiber_pool *pool;
  struct fiber_thread *self;
};

static void __sigusr1_handler(int signum) {
  if (signum != SIGUSR1) {
    return;
  }
  // Will this work? Whow knows!
  sigset_t mask;
  sigfillset(&mask);
  sigdelset(&mask, SIGCONT); // Only allow SIGCONT to wake
  sigsuspend(&mask);
}

static int __worker_threads_start(struct fiber_pool *pool,
                                  struct fiber_thread *head,
                                  tpsize threads_number);
static inline int __worker_pthread_start(struct __pthread_arg *arg);
static void *__worker_loop(void *arg);

static inline int __worker_register_signal_handlers();

static inline void __handle_flags_before_sleep(struct fiber_pool *pool);

int fiber_thread_pool_init(struct fiber_pool *pool, tpsize threads_number) {
  int error_code = 0;
  struct fiber_thread *threads = malloc(threads_number * sizeof(*threads));
  if (threads == NULL) {
    error_code = errno;
    goto err;
  }
  pool->thread_head = threads;
  pool->threads_number = threads_number;
  pool->threads_working = 0;
  pool->pool_flags = 0;

  int sem_res = sem_init(&pool->threads_sync, 0, 0);
  if (sem_res != 0) {
    error_code = errno;
    goto err;
  }

  // Link threads together
  struct fiber_thread *prev = threads++;
  while (threads < pool->thread_head + threads_number) {
    prev->next = threads;
    prev = threads++;
  }
  prev->next = NULL;

  error_code = __worker_threads_start(pool, pool->thread_head, threads_number);
  if (error_code != 0) {
    goto err;
  }

  return 0;
err:
  if (threads != NULL)
    free(threads);
  if (sem_res == 0)
    sem_destroy(&pool->threads_sync);
  return error_code;
}

void fiber_thread_pool_free(struct fiber_pool *pool);

int fiber_threads_remove(struct fiber_pool *pool, tpsize threads_num,
                         uint32_t behavior_flags) {
  return -1;
}

int fiber_threads_add(struct fiber_pool *pool, tpsize threads_num,
                      uint32_t behavior_flags) {
  return -1;
}

tpsize fiber_threads_number(struct fiber_pool *pool) {
  return pool->threads_number;
}

tpsize fiber_threads_working(struct fiber_pool *pool) {
  return pool->threads_working;
}

static int __worker_threads_start(struct fiber_pool *pool,
                                  struct fiber_thread *head,
                                  tpsize threads_number) {
  struct __pthread_arg *args = malloc(threads_number * sizeof(*args));
  if (args == NULL) {
    return errno;
  }
  int error_code = 0;
  tpsize i = 0;
  while (i < threads_number) {
    struct __pthread_arg *arg = &args[i];
    struct fiber_thread *curr = &head[i];
    arg->pool = pool;
    arg->self = curr;
    error_code = __worker_pthread_start(arg);
    if (error_code != 0) {
      goto err;
    }
    ++i;
  }

  return 0;
err:
  // TODO: cancel threads that were started before the error
  return -1;
}

static int __worker_pthread_start(struct __pthread_arg *arg) {
  int error_code =
      pthread_create(&arg->self->thread_id, NULL, __worker_loop, arg);
  if (error_code != 0) {
    return errno;
  }
  return pthread_detach(arg->self->thread_id);
}

static void *__worker_loop(void *arg) {
  struct __pthread_arg *kit = (struct __pthread_arg *)arg;
  struct fiber_pool *pool = kit->pool;
  struct fiber_thread *self = kit->self;
  fiber_queue_pop job_pop = pool->queue_ops->pop;
  struct fiber_job job_buf = {0};

  __worker_register_signal_handlers();

  while (1) {
    int pop_res = job_pop(pool->job_queue, &job_buf, FIBER_BLOCK);
    if (pop_res != 0) {
      sched_yield();
      continue;
    }
    __atomic_fetch_add(&pool->threads_working, 1, __ATOMIC_RELEASE);

    do {
      self->job_id = job_buf.job_id;
      job_buf.job_func(job_buf.job_arg);
    } while (job_pop(pool->job_queue, &job_buf, 0) == 0);
    self->job_id = -1;

    __atomic_fetch_sub(&pool->threads_working, 1, __ATOMIC_RELEASE);
    __handle_flags_before_sleep(pool);
  }

  return NULL;
}

static inline int __worker_register_signal_handlers() {
  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = __sigusr1_handler;
  if (sigaction(SIGUSR1, &sa, NULL) == -1) {
    return errno;
  }
  return 0;
}

static void __handle_flags_before_sleep(struct fiber_pool *pool) {
  // Can this lead to unintended behavior?
  uint32_t pool_flags = __atomic_load_n(&pool->pool_flags, __ATOMIC_RELAXED);
  if (~pool_flags & FIBER_POOL_FLAG_WAIT) {
    return;
  }
  tpsize tworking = __atomic_load_n(&pool->threads_working, __ATOMIC_RELAXED);
  if (tworking > 0) {
    return;
  }
  int sem_res = sem_post(&pool->threads_sync);
  // if (sem_res != 0) {
  //   // TODO: We should kill here in debug because the possible errors
  //   // are not a valid semaphore and overflow
  // }
}
