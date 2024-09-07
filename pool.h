#ifndef _FIBER_POOL_H
#define _FIBER_POOL_H

#include "fiber.h"

int fiber_thread_pool_init(struct fiber_pool *pool, tpsize threads_number);

void fiber_thread_pool_free(struct fiber_pool *pool);

#endif // _FIBER_POOL_H
