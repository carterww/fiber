#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>

#include "fiber.h"
#include "pool.h"

struct pthread_arg {
  struct fiber_pool *pool;
  struct fiber_thread *self;
};

static void sigusr1_handler(int signum);

static int thread_ll_alloc_n(struct fiber_thread **head, tpsize threads_number);
static void thread_ll_free(struct fiber_thread *head);
static inline void thread_ll_add(struct fiber_pool *pool,
                                 struct fiber_thread *head);
static inline int thread_ll_remove(struct fiber_pool *pool,
                                   struct fiber_thread *thread);

static int worker_threads_start(struct fiber_pool *pool,
                                struct fiber_thread *head,
                                tpsize threads_number);
static inline int worker_pthread_start(struct pthread_arg *arg);
static void *worker_loop(void *arg);
static inline int worker_register_signal_handlers();

static inline void handle_flag_wait_all(struct fiber_pool *pool);
static inline void wake_next_sleeping_thread(struct fiber_pool *pool,
                                      struct fiber_thread *head);

static void pthread_cancel_n(struct fiber_thread *head, tpsize threads_number);
static inline void thread_clean_self(struct fiber_pool *pool, struct fiber_thread *self);

int fiber_thread_pool_init(struct fiber_pool *pool, tpsize threads_number) {
  int error_code = thread_ll_alloc_n(&pool->thread_head, threads_number);
  if (error_code != 0) {
    goto err;
  }
  if (pool->thread_head == NULL) {
    error_code = EINVAL;
    goto err;
  }

  int sem_res = sem_init(&pool->threads_sync, 0, 0);
  if (sem_res != 0) {
    error_code = errno;
    goto err;
  }
  pool->threads_number = threads_number;
  pool->threads_working = 0;
  pool->threads_kill_number = 0;

  error_code = worker_threads_start(pool, pool->thread_head, threads_number);
  if (error_code != 0) {
    goto err;
  }
  return 0;
err:
  if (pool->thread_head)
    thread_ll_free(pool->thread_head);
  if (sem_res == 0)
    sem_destroy(&pool->threads_sync);
  return error_code;
}

void fiber_thread_pool_free(struct fiber_pool *pool) {
  pthread_mutex_lock(&pool->lock);
  pthread_cancel_n(pool->thread_head, THREAD_POOL_SIZE_MAX);
  thread_ll_free(pool->thread_head);
  sem_destroy(&pool->threads_sync);
  pthread_mutex_unlock(&pool->lock);
}

int fiber_threads_remove(struct fiber_pool *pool, tpsize threads_num,
                         uint32_t behavior_flags) {
  // Set flag to notify thread it should commit seppuku
  __atomic_fetch_add(&pool->threads_kill_number, threads_num, __ATOMIC_SEQ_CST);
  __atomic_fetch_or(&pool->pool_flags, FIBER_POOL_FLAG_KILL_N,
                    __ATOMIC_SEQ_CST);
  wake_next_sleeping_thread(pool, pool->thread_head);
  return 0;
}

int fiber_threads_add(struct fiber_pool *pool, tpsize threads_num,
                      uint32_t behavior_flags) {
  struct fiber_thread *threads;
  int error_code = thread_ll_alloc_n(&threads, threads_num);
  if (error_code != 0) {
    return error_code;
  }
  thread_ll_add(pool, threads);
  int start_res = worker_threads_start(pool, threads, threads_num);
  if (start_res != 0) {
    return start_res;
  }
  __atomic_fetch_add(&pool->threads_number, threads_num, __ATOMIC_RELAXED);
  return 0;
}

tpsize fiber_threads_number(struct fiber_pool *pool) {
  return __atomic_load_n(&pool->threads_number, __ATOMIC_RELAXED);
}

tpsize fiber_threads_working(struct fiber_pool *pool) {
  return __atomic_load_n(&pool->threads_working, __ATOMIC_RELAXED);
}

static int worker_threads_start(struct fiber_pool *pool,
                                struct fiber_thread *head,
                                tpsize threads_number) {
  struct pthread_arg *args = malloc(threads_number * sizeof(*args));
  if (args == NULL) {
    return errno;
  }
  int error_code = 0;
  tpsize i = 0;
  while (i < threads_number) {
    struct pthread_arg *arg = &args[i];
    struct fiber_thread *curr = &head[i];
    arg->pool = pool;
    arg->self = curr;
    arg->self->job_id = -1;
    error_code = worker_pthread_start(arg);
    if (error_code != 0) {
      goto err;
    }
    ++i;
  }

  return 0;
err:
  if (i > 0)
    pthread_cancel_n(head, i - 1);
  return -1;
}

static int worker_pthread_start(struct pthread_arg *arg) {
  int error_code =
      pthread_create(&arg->self->thread_id, NULL, worker_loop, arg);
  if (error_code != 0) {
    return errno;
  }
  return pthread_detach(arg->self->thread_id);
}

static void *worker_loop(void *arg) {
  struct pthread_arg *kit = (struct pthread_arg *)arg;
  struct fiber_pool *pool = kit->pool;
  struct fiber_thread *self = kit->self;
  fiber_queue_pop job_pop = pool->queue_ops->pop;
  struct fiber_job job_buf = {0};

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
    __atomic_fetch_add(&pool->threads_working, 1, __ATOMIC_RELEASE);

    do {
      __atomic_store_n(&self->job_id, job_buf.job_id, __ATOMIC_RELAXED);
      job_buf.job_func(job_buf.job_arg);
    } while (job_pop(pool->job_queue, &job_buf, 0) == 0);
    __atomic_store_n(&self->job_id, -1, __ATOMIC_RELAXED);

    __atomic_fetch_sub(&pool->threads_working, 1, __ATOMIC_RELEASE);
  handle_flags: {
    uint32_t pool_flags = __atomic_load_n(&pool->pool_flags, __ATOMIC_RELEASE);
    if (pool_flags & FIBER_POOL_FLAG_KILL_N) {
      tpsize to_kill =
          __atomic_load_n(&pool->threads_kill_number, __ATOMIC_RELEASE);
      if (to_kill > 0)
        goto exit_thread;
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
    free(kit);
  pthread_exit(0);
}

static inline int worker_register_signal_handlers() {
  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = sigusr1_handler;
  if (sigaction(SIGUSR1, &sa, NULL) == -1) {
    return errno;
  }
  return 0;
}

static void handle_flag_wait_all(struct fiber_pool *pool) {
  tpsize tworking = __atomic_load_n(&pool->threads_working, __ATOMIC_RELAXED);
  if (tworking > 0) {
    return;
  }
  // TODO: exit here on err because errors caused by post are overflow and not
  // valid sem
  sem_post(&pool->threads_sync);
}

static void wake_next_sleeping_thread(struct fiber_pool *pool,
                                      struct fiber_thread *head) {
  int lock_res = pthread_mutex_lock(&pool->lock);
  if (lock_res != 0) {
    return;
  }
  while (head != NULL) {
    jid curr_job_id = __atomic_load_n(&head->job_id, __ATOMIC_RELEASE);
    if (curr_job_id < 0) {
      // Wake up thread to handle new flags
      pthread_kill(head->thread_id, SIGCONT);
      break;
    }
    head = head->next;
  }
  pthread_mutex_unlock(&pool->lock);
}

static int thread_ll_alloc_n(struct fiber_thread **head,
                             tpsize threads_number) {
  *head = malloc(sizeof(**head));
  if (*head == NULL) {
    return errno;
  }
  struct fiber_thread *curr = *head;
  ;
  while (curr < *head + threads_number - 1) {
    curr->next = malloc(sizeof(*curr));
    if (curr->next == NULL) {
      return errno;
    }
    curr = curr->next;
  }
  curr->next = NULL;
  return 0;
}

static void thread_ll_free(struct fiber_thread *head) {
  struct fiber_thread *curr = head;
  struct fiber_thread *next;
  while (curr != NULL) {
    next = curr->next;
    free(curr);
    curr = next;
  }
}

static inline void thread_ll_add(struct fiber_pool *pool,
                                 struct fiber_thread *head) {
  int lock_res = pthread_mutex_lock(&pool->lock);
  if (lock_res != 0 || lock_res != EDEADLK) {
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
                                   struct fiber_thread *thread) {
  int res = -1;
  if (pool->thread_head == NULL) {
    return res;
  }
  int lock_res = pthread_mutex_lock(&pool->lock);
  if (lock_res != 0 || lock_res != EDEADLK) {
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

static void sigusr1_handler(int signum) {
  if (signum != SIGUSR1) {
    return;
  }
  // Will this work? Whow knows!
  sigset_t mask;
  sigfillset(&mask);
  sigdelset(&mask, SIGCONT); // Only allow SIGCONT to wake
  sigsuspend(&mask);
}

static void pthread_cancel_n(struct fiber_thread *head, tpsize threads_number) {
  tpsize i = 0;
  while (head != NULL && i < threads_number) {
    pthread_cancel(head[i++].thread_id);
  }
}

static inline void thread_clean_self(struct fiber_pool *pool, struct fiber_thread *self) {
  __atomic_fetch_sub(&pool->threads_kill_number, 1, __ATOMIC_RELAXED);
  struct fiber_thread *next = self->next;
  int found = thread_ll_remove(pool, self);
  if (found == 0) {
    free(self);
  }
  __atomic_fetch_sub(&pool->threads_number, 1, __ATOMIC_RELAXED);
  tpsize to_kill =
    __atomic_load_n(&pool->threads_kill_number, __ATOMIC_RELEASE);
  if (to_kill > 0) {
    wake_next_sleeping_thread(pool, next);
  }
}
