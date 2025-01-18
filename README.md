# Fiber
Fiber is a thread pool library that uses POSIX Threads (pthreads). Fiber is a rewrite of [Schwimmbad](https://github.com/carterww/schwimmbad), my previous attempt at a thread pool library. Fiber's features include:
1. A default queue implementation that can easily be replaced.
2. The ability to add and remove threads after initialization.
3. The ability to wait for all jobs to be completed.
4. The ability to use custom memory allocators.
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

# Planned Updates
1. **Cleaning up the public interface**: (*Completed*) fiber.h exposes too many unnecessary details. I'd like to split the fiber.h file into fiber.h and fiber_internal.h. This creates a clear separation between the public interface and the implementation.
2. **Enable more compiler warnings and compile with 0 warnings**: Self explanatory. This is good practice.
3. **Enable -pedantic where possible**: To promote portability, we should attempt to compile as much code as possible with the pedantic flag. This ensures we are not using non-portable compiler extensions.
4. **Switch to ANSI C (Maybe)**: I like the idea of the code being as portable as possible. This is a maybe because it would require a lot of work with little immediate payoff.
5. **Move configuration options to config.h**: Self explanatory.
6. **Decouple from POSIX threads**: The calls to the underlying threads API (pthreads, whatever Windows uses, etc.) should be put behind an interface.
7. **Add proper versioning**: Add versioning information to the header AND compiled binary.
8. **Add the compile time configuration to binary**: Users could test if some options were enabled. This is helpful if users are using a prebuilt binary instead of compiling from source.
9. **Upgrade the build system**: I'm not sure what I'll do yet but I feel like the current system is bad.
10. **Change the testing system**: Similar to (9).
11. **Add more test cases**: Can never have too many.
12. **Test with Thread Sanitizer**: Clang library used for detecting race conditions.
