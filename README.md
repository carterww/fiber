# Fiber
Fiber is a thread pool library built on top of POSIX Threads (pthreads) API. Even though it
only supports pthreads right now, it can be modified to use other threading APIs somewhat
easily.

## API
Each function that makes up Fiber's API can be found in [fiber.h](include/fiber/fiber.h). Each
function has a comment above its prototype describing its behavior, parameters, and
return value.

fiber.h does not provide prototypes for queue functions. These will be found in separate header
file(s).

## Writing a Custom Job Queue
Fiber provides a default [job queue implementation](include/fiber/fiber_fifo.h) that should fulfil most needs,
but you can easily integrate a custom job queue into Fiber. If you are interested in writing
your own job queue for fiber, please read the [requirements](src/queue/README.md).

## Selling Points
Some of Fiber's selling points include:
1. A simple and straightforward interface.
2. Ability to use custom memory allocators.
3. No hidden memory allocation.
4. Ability to select a job queue implementation at initialization.
5. Support for adding and removing threads after initialization.
6. Minimal lock contention and small critical sections.
7. Designed for portability.

### Simple and Straightforward Interface
This is hard to quantify, but I'd like to think all the functions in [fiber.h](include/fiber/fiber.h) are
clear and concise with adequate documentation.

### Custom Memory Allocators
fiber_init takes function pointers to malloc and free that are used by the pool and queue.
Using libc's malloc and free is probably okay in 95% of use cases, but the option to use custom
memory allocators gives the user flexibility.

### No Hidden Memory Allocations
Fiber's background threads never allocate memory (unless the job it's running does). Only these function
calls will allocate memory:
1. **fiber_init**: Allocates memory for the pool, queue, linked list of threads, and each thread's
   arguments.
2. **fiber_threads_add**: Allocates memory for the linked list of new threads and each thread's arguments.
3. **fiber_job_push and fiber_job_push_raw**: This may or may not allocate memory depending on
   the queue implementation. The default FIFO queue does not allocate memory after initialization, but
   I list these functions here just in case your queue does.
4. **fiber_free**: This may seem weird but allocating a temporary list for thread IDs when canceling
   and joining threads results in a 3-4x speedup of fiber_free. I'm not sure if this is worth it.

Hidden memory allocation may be a problem if your application has somewhat strict latency requirements.

### Job Queue Selection
Fiber only has one job queue implementation right now, but I plan to add at least one more. Beyond the
job queues provided by Fiber, a user can easily implement their own and use it alongside the provided
queue (see [how](src/queue/README.md)).

### Adding/Removing Threads After Initialization
Threads can be added and removed from the pool after initialization. This allows a pool to be
scaled appropriately without needing to create a new pool or free an existing one.

### Minimal Lock Contention and Small Critical Sections
Locks are used in two places:
1. Adding or removing threads from the pool. Threads are in a linked list and the head is
   protected by a mutex.
2. Fetching and incrementing the head/tail pointers in the FIFO queue implementation.

The first case is very rare: it only affects dynamic thread scaling and thread cleanup
routines. The second case is very common, but the critical section is made up of a load,
store, addition, and modulo. There is also an implicit memory barrier on either side of
that sequence so the cost is greater than it may appear.

Fiber heavily relies on atomic operations instead of locks for frequently accessed/updated
variables. This is usually faster, but it comes with a major downside: these variables can't
be trusted after loading them. To see a prime example of this, read the code and comments for
[fiber_wait](src/fiber.c).

My goal is to write a lock free queue for Fiber after testing to eliminate the second case
listed above.

### Designed for Portability
Fiber is written in C89 and the source attempts to stick to it. Calls to nonstandard
functions are hidden in files that can be swapped out at build time like
[atomic_gcc_clang.c](src/atomic_gcc_clang.c) and [threading_pthread.c](src/threading_pthread.c).
The former hides builtin atomic functions behind an interface and the latter hides
mutex, semaphore, and pthread threading functions behind an interface.

Some tests and the [example](src/example.c) do not hide nonstandard function calls
(mostly time related stuff).

## Versioning
Fiber provides definitions for each version number required by Semantic Versioning 2.0.0.
These are defined in fiber.h as FIBER_VERSION_\[MAJOR|MINOR|PATCH\].

There is also a struct defined in src/fiber.c that contains the major, minor, and patch of
the current build that can be accessed by fiber_libversion(). The function
fiber_libversion_compatible() can be used to ensure your project is using a header file
that is compatible with the library's version. Calling this function on startup is
recommended (especially if you are using a shared object file).

## TODOS
A list of things that must be completed before 1.0.0.
1. Write unit tests for every API function. These should test fault handling, improper
   arguments, and the happy paths.
2. Write integration tests for units that are heavily coupled.
      - job_queue_pop and job_queue_push
      - fiber_wait and workers threads
3. Write broad performance tests that compare against other C thread pool libraries.
4. Microbenchmark hot paths of code.
      - job_queue_push, job_queue_pop, fiber_worker_loop, fiber_worker_execute_job,
        flag handling functions, fiber_thread_cleanup.
5. Test/look for race conditions. Clang's thread sanitizer? Other ways?
      - I know there has to be at least one lying around.
6. Rely less on panic and fiber_assert in places where errors are possible. These should
   be handled (if possible) or passed to the caller.
7. Implement a lock free job queue and performance test it against the current FIFO
   implementation. 
8. Weigh the costs and benefits of including a callback mechanism.
      - Would it have any use?
      - Should Fiber rely on the user to implement their own?
      - Should it be per pool or per job?
