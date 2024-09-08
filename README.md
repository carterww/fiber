# Fiber - A Better Schwimmbad
Fiber is a thread pool library built on top of POSIX pthreads. The project is not complete.
# Design Phase
This section will be a place to throw all my thoughts related to the program's design.
## Functional Requirements
1. The user should be able to create multiple thread pools at runtime.
    - The user should be able to some parameters at this phase to configure the pool:
        1. The job queuing implementation.
        2. The initial number of pthreads to spin up.
    - Each pool should be completely independent from other thread pools in the same process space.
2. The user should be able to implement their own queuing strategy before compile time.
    - Fiber should be queuing strategy agnostic.
3. Fiber should provide at least one default queuing implementation.
    - If the user does not specify a strategy, one will be automatically selected.
4. The user should be able to push jobs onto a queue that automatically get executed by pthreads.
5. Fiber's default job queue should provide blocking and non-blocking options for pushing and popping jobs.
    - If the user decides to implement their own queue, they must add this support to enhance performance.
6. The user should be able to add or remove pthreads at runtime.
    - A call to remove thread(s) should NOT cancel any in progress user jobs.
    - It should prioritize idle threads. If there are not enough idle threads to fulfill the request, it should wait until a user job finishes.
7. The user should be able to wait on all jobs to finish in a blocking manner.
    - This should not prevent jobs from being added to the queue.
8. There will be no implementation for removing jobs from the queue.
9. There will be no implementation for cancelling executing jobs.
## Non-Functional Requirements
1. The data structure for storing threads should be a singly linked list.
    - Fiber should attempt to keep groups of threads in contiguous areas of memory. They should be allocated in "extents" to maximize cache hits.
2. Fiber's thread pool should be completely decoupled from any queue implementation details.
    - The queue operations should be a struct of function pointers set at the pool initialization time.
3. Fiber should assign an id of some integer type to a job before putting it on the queue.
4. In the default job queue implementation, the wall clock time to push a job, pull it off the empty queue, and start execution should be quicker than spinning up a pthread and executing a function.
5. The only assumption Fiber should make about the system is that POSIX pthreads, POSIX mutexes, POSIX semaphores, and some common atomic operations are available.
    - Atomic integers, compare and swap, and possibly others will be used. The compiler make these available to Fiber.
6. The default job queue should be a FIFO queue in a contiguous memory space. It will not be resizable after allocation.
