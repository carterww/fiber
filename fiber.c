#include <stdlib.h>

#include "fiber.h"

int fiber_init(struct fiber_pool *pool, struct fiber_pool_init_options *opts) {
  return -1;
}

jid fiber_job_push(struct fiber_pool *pool, struct fiber_job *job,
                   uint32_t queue_flags) {
  return -1;
}

int fiber_free(struct fiber_pool *pool, uint32_t behavior_flags) { return -1; }

void fiber_wait(struct fiber_pool *pool) {}

void fiber_pause(struct fiber_pool *pool) {}

void fiber_resume(struct fiber_pool *pool) {}

qsize fiber_jobs_pending(struct fiber_pool *pool) {
  if (pool == NULL || pool->job_queue == NULL ||
      pool->queue_ops == NULL || pool->queue_ops->length == NULL) {
    return 0;
  }
  return pool->queue_ops->length(pool->job_queue);
}
