# Fiber
Fiber is a thread pool library that uses POSIX Threads (pthreads). Fiber is a rewrite of [Schwimmbad](https://github.com/carterww/schwimmbad), my previous attempt at a thread pool library. I made some poor desing choices with Schwimmbad that I attempted to correct here. Fiber's features include:
1. Easy thread management.
2. A default queue implementation that can easily be swapped out for a custom queue.
3. The ability to add and remove threads after initialization.
4. The ability to wait for all jobs to be completed.
5. The ability to use custom memory allocators.
If you decide to use this library and encounter any bugs, please submit an issue.
## API
Each function's behavior is thoroughly documented in [fiber.h](fiber.h).
# Writing a Custom Queue
Fiber makes it easy to provide a custom queue implementation at thread pool initialzation time. Before diving into it, check out [job_queue.h](job_queue.h) and [fifo_job_queue.c](queue_impls/fifo_job_queue.c) to see the queue API Fiber expects.
## Requirements
There are a couple of behaviors Fiber expects in order to make the job queue integrate well with the thread pool.
1. The *push* and *pop* functions should **NOT** use any of the MSb in *uint32_t flags*. Right now, this is used for blocking behavior.
2. The *pop* function **SHOULD** check for the flag FIBER_BLOCK and block when there are no jobs to execute. If FIBER_BLOCK is not provided, it should return a value of zero to indicate *buffer* has a job and a non-zero value to indicate there are no jobs.
    - To see why, inspect the *worker_loop* function in [fiber.c](fiber.c).
3. The *push* function should never return a postive number to indicate failure. *Push* is used by fiber_job_push and a positive return value from this corresponds to a valid job id.
    - To see why, inspect the *\__fiber_job_push* function in [fiber.c](fiber.c).
If your queue meets these requirements, it will integrate nicely with Fiber. These functions can be passed to *fiber_init* through the *fiber_init_options* struct.
