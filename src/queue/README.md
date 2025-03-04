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
- *push* and *pop* should not use bits 24-31 in the flags. Only one is currently
  used but the others are reserved for future additions.
   - Bits 0-23 can be used for any custom behavior.
- *push* and *pop* MUST respect the FIBER_QUEUE_BLOCK flag. If this flag is set,
  the function should block until the operation can be completed. If it is not set,
  it should return FBR_EAGAIN if the operation cannot be completed immediately.
   - For *push*, this means it should block until the job is pushed onto the queue.
   - For *pop*, this means it should block until a job is available to remove and
     execute.
      - Blocking on *pop* is especially useful because it allows the thread to sleep
        until a job is ready (if blocking = sleeping).
- If the function returns an int, a 0 value should be returned to indicate a success
  and a non-zero should be returned to indicate an error. An error value defined in
  fiber.h is preferred, but you can define custom errors that do not conflict with those.
- *pop* should only return an error if FIBER_QUEUE_BLOCK is not supplied and the operation
  cannot be completed.
- *push* can return a custom error that will be returned by fiber_job_push or
  fiber_job_push_raw.
- *push* must copy the contents of the job into its own data structure(s). It should not
  store a reference to the job argument in any way.

## Adding your Queue
In order to completely integrate your queue, you must add it to the build system and
optionally add it as a capability. Adding it as a capability will allow your application(s)
to ensure Fiber was compiled with your queue (this is only recommended if Fiber is being
used as a shared object file).

### Build System
In order to add your queue to the build follow these steps:
1. Add a new boolean option to config.mk with the name COMPILE_FIBER_\[QUEUE_NAME\]_QUEUE.
2. Add an "ifeq" check in common.mk that adds src/queue/\[QUEUE_NAME\].o to QUEUE_OBJS if the
   option defined in config.mk is true.

The steps above will ensure your queue is compiled into Fiber's final binary, but it will
not add the queue as a capability.

### Capability
Fiber provides a function, *fiber_capability_get*, that allows applications to check if
Fiber supports a feature at runtime. Usually these features are optionally compiled into
Fiber (like your queue), so it is helpful to check for support if your application relies
on one of these features.

Follow these steps to to add your queue as a capability:
1. Add a new definition to C_CONFIG_FLAGS in common.mk. It should take the following form:
   -D"FIBER_COMPILE_FIBER_\[QUEUE_NAME\]_QUEUE=($(COMPILE_FIBER_\[QUEUE_NAME\]_QUEUE))".
2. Locate the enum *fiber_capability_option* in fiber.h and add a new member just before
   FIBER_CAPABILITY_ENUM_END named FIBER_CAPABILITY_FIBER_\[QUEUE_NAME\]_QUEUE. You should
   also set the value to the previous value + 1. This isn't required but being explicit
   helps with the future steps.
3. Go to src/capability.c and locate the *capability_bitstring* array. This array stores
   each capability option as a bit.
4. Locate the array element which corresponds to the value you just set. There should be a
   comment above each element like "0-7 fiber_capability_option values." If there is not an
   element for your option, add one.
5. Bitwise OR the following to the other values: "CAPABILITY_BIT(FIBER_COMPILE_FIBER_\[QUEUE_NAME\]_QUEUE,
   FIBER_CAPABILITY_FIBER_\[QUEUE_NAME\]_QUEUE)"

## Further Information
If you are trying to add your own queue to Fiber and have any questions or need help, please
submit an issue on GitHub. You can also shoot me an email at carterww@hotmail.com.
