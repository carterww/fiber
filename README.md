# Fiber
Fiber is a thread pool library that uses POSIX Threads (pthreads). Fiber is a rewrite of
[Schwimmbad](https://github.com/carterww/schwimmbad), my previous attempt at a thread pool
library. Fiber's features include:
1. A default queue implementation that can easily be replaced.
2. The ability to add and remove threads after initialization.
3. The ability to wait for all jobs to be completed.
4. The ability to use custom memory allocators.

## API
Each function's behavior is thoroughly documented in [fiber.h](fiber.h).

## Writing a Custom Job Queue
Fiber provides a default [job queue implementation](fiber_fifo.h) that should fulfil most needs,
but you can easily integrate a custom job queue into Fiber. If you are interested in writing
your own job queue for fiber, please read the [requirements](src/queue/README.md).

## Planned Updates
1. **Cleaning up the public interface**: (*Completed*) fiber.h exposes too many unnecessary
   details. I'd like to split the fiber.h file into fiber.h and fiber_internal.h. This creates a
   clear separation between the public interface and the implementation.
2. **Enable more compiler warnings and compile with 0 warnings**: (*Completed*) Self explanatory. This is good practice.
3. **Enable -pedantic where possible**: (*Completed*) To promote portability, we should attempt to compile as much
   code as possible with the pedantic flag. This ensures we are not using non-portable compiler extensions.
4. **Switch to ANSI C (Maybe)**: (*Completed*) I like the idea of the code being as portable as possible. This is a maybe
   because it would require a lot of work with little immediate payoff.
5. **Move configuration options to config.h**: (*Completed* but in config.mk) Self explanatory.
6. **Decouple from POSIX threads**: (*Completed*) The calls to the underlying threads API (pthreads, whatever Windows uses, etc.)
   should be put behind an interface.
7. **Add proper versioning**: (*Completed*) Add versioning information to the header AND compiled binary.
8. **Add the compile time configuration to binary**: (*Completed*) Users could test if some options were enabled. This
   is helpful if users are using a prebuilt binary instead of compiling from source.
9. **Upgrade the build system**: (*Completed* I stuck with Make but made the build system more flexible) I'm not sure
   what I'll do yet but I feel like the current system is bad.
10. **Change the testing system**: Similar to (9).
11. **Add more test cases**: Can never have too many.
12. **Test with Thread Sanitizer**: Clang library used for detecting race conditions.

## Versioning
Fiber provides definitions for each version number required by Semantic Versioning 2.0.0.
These are defined in fiber.h as FIBER_VERSION_\[MAJOR|MINOR|PATCH\]. There is also a struct
defined in src/fiber.c that contains the major, minor, and patch of the current build that can be
accessed by fiber_libversion(). The function fiber_libversion_compatible() can be used to ensure your
project is using a header file that is compatible with the library's version. Calling this function on
startup is recommended (especially if you are using a shared object file).
