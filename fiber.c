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

int fiber_threads_remove(struct fiber_pool *pool, tpsize threads_num,
                         uint32_t behavior_flags) {
  return -1;
}

int fiber_threads_add(struct fiber_pool *pool, tpsize threads_num,
                      uint32_t behavior_flags) {
  return -1;
}

tpsize fiber_threads_number(struct fiber_pool *pool) { return 0; }

tpsize fiber_threads_working(struct fiber_pool *pool) { return 0; }

void fiber_wait(struct fiber_pool *pool) {}

void fiber_pause(struct fiber_pool *pool) {}

void fiber_resume(struct fiber_pool *pool) {}

qsize fiber_jobs_pending(struct fiber_pool *pool) { return 0; }
