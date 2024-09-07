#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

#include "fiber.h"
#include "job_queue.h"
#include "pool.h"

static inline int valid_queue_ops(struct fiber_queue_operations *ops) {
  return ops == NULL || ops->pop == NULL || ops->push == NULL ||
         ops->init == NULL || ops->free == NULL;
}

static inline jid get_and_update_jid(struct fiber_pool *pool);

int fiber_init(struct fiber_pool *pool, struct fiber_pool_init_options *opts) {
  // TODO: make error codes for these separate
  if (pool == NULL || opts == NULL) {
    return EINVAL;
  }
  if (opts->threads_number == 0 || opts->queue_length == 0) {
    return EINVAL;
  }
  if (valid_queue_ops(opts->queue_ops) != 0) {
    return EINVAL;
  }
  int error_code = 0;
  int mutex_res = pthread_mutex_init(&pool->lock, NULL);
  if (mutex_res != 0) {
    error_code = mutex_res;
    goto err;
  }
  int queue_res = pool->queue_ops->init(&pool->job_queue, opts->queue_length);
  if (queue_res != 0) {
    error_code = queue_res;
    goto err;
  }
  if (pool->job_queue == NULL) {
    error_code = EINVAL;
    goto err;
  }
  pool->job_id_prev = -1;
  pool->queue_ops = opts->queue_ops;
  pool->pool_flags = 0;
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

  return error_code;
}

jid fiber_job_push(struct fiber_pool *pool, struct fiber_job *job,
                   uint32_t queue_flags) {
  if (pool == NULL || job == NULL) {
    return EINVAL;
  }
  job->job_id = get_and_update_jid(pool);
  int push_res = pool->queue_ops->push(pool->job_queue, job, queue_flags);
  if (push_res != 0) {
    return -1;
  }
  return job->job_id;
}

int fiber_free(struct fiber_pool *pool, uint32_t behavior_flags) { return -1; }

void fiber_wait(struct fiber_pool *pool) {}

void fiber_pause(struct fiber_pool *pool) {}

void fiber_resume(struct fiber_pool *pool) {}

qsize fiber_jobs_pending(struct fiber_pool *pool) {
  if (pool == NULL || pool->job_queue == NULL || pool->queue_ops == NULL ||
      pool->queue_ops->length == NULL) {
    return 0;
  }
  return pool->queue_ops->length(pool->job_queue);
}

static inline jid get_and_update_jid(struct fiber_pool *pool) {
  // 64 bits will probably never overflow but 32 or less may
#if JOB_ID_MAX < INT64_MAX || FIBER_CHECK_JID_OVERFLOW != 0
  jid new;
  // Grab job_id_prev first time. After this, atomic_cmp_ex will load prev with
  // the current job_id_prev if it fails.
  jid prev = __atomic_load_n(&pool->job_id_prev, __ATOMIC_SEQ_CST);
  // Detect overflow and correct. Must be done in atomic compare and exchange
  do {
    new = prev == JOB_ID_MAX ? -1 : prev + 1;
  } while (!__atomic_compare_exchange_n(&pool->job_id_prev, &prev, new, 0,
        __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
#endif
  return __atomic_add_fetch(&pool->job_id_prev, 1, __ATOMIC_RELEASE);
}
