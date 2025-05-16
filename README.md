# Fiber
Fiber is a lock-free thread pool library written in C99 using [Concurrency Kit](https://concurrencykit.org/).
Fiber currently only supports POSIX threads, but it allows an external thread to join the pool at any time.
Some of Fiber's highlights include:
1. **No Locks**: No need to worry about deadlocks or unpredictable latency spikes.
2. **No Memory Allocation after Initialization**: All memory needed by the thread pool is allocated at
   initialization time.
3. **Swappable Job Queues**: Select a job queue that suits your needs at initialization. Only one queue
   implementation is supported, but more are on the way.
4. **Portability**: Fiber is built with portability in mind for UNIX-like systems and embedded
   devices. Currently supports Linux with more platforms on the way.
5. **Battle-tested Concurrency Primitives**: [Concurrency Kit](https://concurrencykit.org/) is a reliable
   library used in places like the FreeBSD kernel.
6. **Support for External Threads**: User-created threads can join a pool at any time. This can be useful if
   you have spare threads or POSIX threads are not supported.

## API
Fiber's public API can be found in the [include directory](include).
- **[fbr.h](include/fbr.h)**: Main header file that includes all thread pool functions.
- **[fbr_errno.h](include/fbr_errno.h)**: Header file that contains error codes returned by Fiber's functions.
- **[fbr_jq_ring.h](include/fbr_jq_ring.h)**: Header file that contains the function definitions for the
  fixed-size ring buffer job queue.

I'm working on creating documentation for each function, but I want to ensure the API is mostly finalized
before I do that.

## TODOS
A list of things that must be completed before 1.0.0.
- Switch from EBR to HP for wait and wait_job entries.
- Verify all atomic ops and fences to ensure race conditions are not present.
- Write documentation for public API.
- Write tests for every API function. These should test fault handling, improper arguments, and the
  happy paths.
- Write broad performance tests that compare against other C thread pool libraries.
- Microbenchmark hot paths of code.
- Rely less on panic and fiber_assert in places where errors are possible. These should
  be handled (if possible) or passed to the caller.
- Implement a dynamically sized job queue.
