# Implementing a Queue
Implementing your own job queue in Fiber is fairly straightforward, but it comes
with a few requirements to ensure threads can sleep or block if no jobs are
available.

## A Pool's Queue
The queue is made up of two members in the pool:
1. A void pointer to the queue struct. This is passed to each queue function.
2. A VTable of the type struct fiber_queue_operations. These are the functions
   the pool calls when interacting with the queue.
The VTable is provided by the user in fiber_init. For this reason, the queue's header
file should be a part of the public API (notice fiber_fifo.h is in the project's root
directory). This allows the user to pass you queue functions to fiber_init.

### VTable Functions
A queue's VTable is made up of 5 functions:
1. **init**: Initializes the queue by allocating any necessary resources.
2. **free**: Frees up any resources allocated by the pool.
3. **push**: Pushes a job onto the queue to be popped at a later point.
4. **pop**: Pops a job off the queue to be executed.
5. **length**: Returns the number of jobs in the queue.

## Implementation Requirements
As stated previously, Fiber expects certain behavior from a queue. If all these
requirements are met, your queue should seamlessly integrate into Fiber.
- *push* and *pop* should not use the most significant bit in *flags*. This bit
  is already used to indicate the function should block.
   - Any of the other bits can be used to define custom behavior.
- *push* and *pop* MUST respect the FIBER_QUEUE_BLOCK flag. If this flag is set,
  the function should block until the operation can be completed. If it is not set,
  it should return an error if the operation cannot be completed immediately.
   - For *push*, this means it should block until the job is pushed onto the queue.
   - For *pop*, this means it should block until a job is available to pop off.
      - Blocking on *pop* is especially useful because it allows the thread to sleep
        until a job is ready (if blocking = sleeping).
- *push* and *pop* should return FBR_ETHREADING_EAGAIN if the block flag is not set
  and the operation cannot be completed immediately.
- If the function returns an int, a 0 value should be returned to indicate a success
  and a non-zero should be returned to indicate an error. An error value defined in
  fiber.h is preferred, but you can define custom errors that do not conflict with those.
- *push* must copy the contents of the job into its own data structure(s). It should not
  store a reference to the job argument in any way.
