# Fiber
Fiber is a lock-free thread pool library written in C99 using [Concurrency Kit](https://concurrencykit.org/).
Fiber currently only supports platforms with support for POSIX threads, but it allows an external thread to
join the pool at any time. Some of Fiber's highlights include:
1. **No Locks**: No need to worry about deadlocks or unpredictable latency spikes.
2. **No Memory Allocation After Initialization**: All memory needed by the thread pool is allocated at
   initialization time.
3. **Swappable Job Queues**: Select a job queue that suits your needs at initialization. Only one queue
   implementation is supported, but more are on the way.
4. **Portability**: Fiber is built with portability in mind for UNIX-like systems and embedded
   devices. Currently supports Linux with more platforms on the way.
5. **Battle-Tested Concurrency Primitives**: [Concurrency Kit](https://concurrencykit.org/) is a reliable
   library used in places like the FreeBSD kernel.
6. **Support for External Threads**: User-created threads can join a pool at any time. This can be useful if
   you have spare threads or POSIX threads are not supported.
7. **Dynamic Resizing**: Threads can be added or removed after initialization.
8. **Support for Waiting on Jobs or the Pool to Complete**: Callers can synchronize with the pool by waiting
   for a specific job to finish or for all jobs to complete.

## API
Fiber's public API can be found in the [include directory](include).
- **[fbr.h](include/fbr.h)**: Main header file that includes all thread pool functions.
- **[fbr_errno.h](include/fbr_errno.h)**: Header file that contains error codes returned by Fiber's functions.
- **[fbr_jq_ring.h](include/fbr_jq_ring.h)**: Header file that contains the function definitions for the
  fixed-size ring buffer job queue.

## Motivation
Around March 2024 I wanted to write an HTTP server from scratch in C to learn more about the protocol.
I quickly realized that spawning and destroying a thread on every request was a bad pattern, and I needed
a way to give work to a group preallocated threads. After numerous iterations of a thread pool library, I
finally have a version that I think is sufficient.

## TODOS
A list of things that must be completed before 1.0.0.
- Add "futex" implementations for macOS and FreeBSD.
- Verify all atomic ops and fences to ensure race conditions are not present.
- Write tests for every API function. These should test fault handling, improper arguments, and the
  happy paths.
- Write broad performance tests that compare against other C thread pool libraries.
- Microbenchmark hot paths of code.
- Rely less on panic and fiber_assert in places where errors are possible. These should
  be handled (if possible) or passed to the caller.
- Implement a dynamically sized job queue.
