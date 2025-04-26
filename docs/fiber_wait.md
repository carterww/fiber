# fiber_wait Implementation
This function has proven difficult to implement effectively for a couple of reasons:
1. Threads execute jobs and update the pool's state in a lock-free manner which makes
   it difficult to pin down the pool's state for any period of time.
2. There can be N waiters and M wakers where N, M >= 0. There is no guarantee that there
   will be a waker available to wake a waiter. This can lead to a deadlock if not handled
   carefully.

This document will provide a detailed overview of the new implementation.

## Implementation Details
A waiter thread will call fiber_wait at time $t_{i}$ to block until the thread pool is finished
executing all the jobs in the queue. The waiter asks Fiber to wake it up at some time
$T \geq t_{i}$ the condition $C$ is met:
```math
C_{1} = threads\_number = 0
```
```math
C_{2} = threads\_working = 0 \; and \; queue\_length = 0
```
```math
C = C_{1} \; or \; C_{2}
```

$C_{1}$ is by necessity: if there are not threads in the pool then no wakers will be available
to wake the waiter. $C_{2}$ requires the thread pool to have finished all of its work.

The waiter should perform the following actions:
1. Declare its intent to wait on the pool to the wakers.
2. Atomically fetch the number of threads in the pool. If it is 0, return because $C_{1}$ is met.
3. Atomically fetch the number working threads and queue length. If they are both 0, return because
   $C_{2}$ is met.
4. Sleep until a waker wakes the waiter and return.

There are 5 major implementation details that need to be defined for the actions above to work:
1. How do we atomically declare the intent to wait on the pool?
2. How do we destroy this intent and who is responsible for destroying it?
3. How do we atomically fetch the number of working threads and queue length at the same time?
4. How do the waiters go to sleep?
5. How do the wakers wake the waiters?

### Intent

### Checking the Condition
Before a waiter can go to sleep, it must atomically check $C$. Doing so ensures the waiter will not
go sleep when nobody is available to wake it up. Because $C$ is the logical or of $C_{1}$ and $C_{2}$,
short-circuit evaluation can be utilized. If either is true, $C$ is true and the waiter can wake up
and return.

#### Checking $C_{1}$
Multiple threads can modify the thread count, so threads_number **must** be fetched atomically. If it
is 0, $C_{1}$ is true and the thread can exit. If it is not 0, $C_{1}$ is false and the thread must
check $C_{2}$.

#### Checking $C_{2}$
Multiple threads can also modify the thread working count and queue length, so these must be fetched
atomically as well. The problem is we must atomically fetch both at the same time $t_{j}$ to ensure
they are 0 at the same time.

Imagine we fetch the thread working count at $t_{j}$ and the queue length at $t_{j + 1}$. We cannot
say with any certainty that $C_{2}$ holds at $t_{j}$ or $t_{j + 1}$ because we simply do not know
anything about the other variable at those times. This means we must do one of the following:
1. Fetch both counts at the same time $t_{j}$.
2. Fetch one count at $t_{j}$ and the other at $t_{j + 1}$ but ensure the first count cannot be
   modified in the interval \[ $t_{j}$, $t_{j + 1}$ ].

##### twql_packed
Some architectures provide a means for atomically fetching two words. I want Fiber to be as portable
as possible with as little work as possible. Fiber already assumes the architecture supports atomic
operations on at most 1 word. So, let's just pack the thread working count and queue length into
1 word and atomically fetch that! This scheme ensures we get a we can evaluate $C_{2}$ at a single
point in time. But how do we increment and decrement the counters?

Packing the thread working count and queue length into 1 word makes increment and decrement operations
on the individual components harder but not impossible. A simple CAS loop can be used to modify
the packed word. See [twql_packed.h](/src/twql_packed.h) for more details.

#### Declaring the Intent Before Checking the Condition
A waiter must declare its intent to wait before checking the condition and possibly returning early.
I think the need for this becomes very clear through an example.

Assume a waiter calls fiber_wait when there is 1 thread in the pool, 1 thread working, and 0 jobs
in the queue. The waiter find $C_{1}$ and $C_{2}$ to be false so it prepares to sleep, but before it
can sleep, the OS preempts it. In this time, the values fetched for $C_{1}$ and $C_{2}$ become stale.
Let's say the last thread in the pool exits before the OS reschedules the waiter thread. Now $C_{1}$
and $C_{2}$ become true, but the waiter does not know that. It then goes to sleep, but no thread will
be available to wake it up.

If the waiter declared its intent before checking $C_{1}$ and $C_{2}$ the last thread could've modified
the intent before it exited in such a way that the waiter would not go to sleep.

#### What if $C_{1}$ or $C_{2}$ Become True After Evaluation?
This is where the waiter must assume a waker will wake it up once either condition becomes true. We
know this is true because $C_{1}$ and $C_{2}$ were false **after** the waiter declared its intent.
That means there must be at least one thread to wake the waiter **and** that thread will (or did)
reach the code to notify the waiter. The second condition is a consequence of the following cases:
1. A thread was executing a job (working). It always checks if there are waiters before sleeping
   or exiting.
2. A thread will eventually work because there is a job in the queue. After it pulls a job from the
   queue, this becomes case 1.

### Sleeping and Waking
