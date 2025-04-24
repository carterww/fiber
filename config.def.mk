# Boring options
CC = cc
TARGET = fiber

# Compile time options that the user can configure. For now, there are two types:
# booleans: These should be represented as 0 or 1
# enum: These should be represented as one of the values commented above it.

# enum { norm | debug | test }
# Build environment
# norm: Default build that should be used for production.
# debug: Debug build that has symbols built in.
# test: Special build that builds some code only used when testing.
ENV=norm

# enum { thread | address | none }
# Sanitizer to use when compiling in debug or test environments.
# thread: Use clang's or gcc's thread sanitizer. 
# address: Use clang's or gcc's address sanitizer and other memory related
# sanitizers.
DEBUG_SANITIZE=none

# boolean
# Whether to produce position-independent code (pic). Shared libraries must use
# PIC so this flag will not affect the lib_so target.
PIC=1

# boolean
# Compile fiber_assert statements
COMPILE_ASSERTS=1

# boolean
# Compile the default FIFO queue (include/fiber/fiber_fifo.h and src/queue/fifo.c)
COMPILE_FIBER_FIFO_QUEUE=1

# enum { pthread }
# Underlying threading library to use
THREADING_LIB=pthread

# enum { gcc | clang }
# Atomic operation implementation to use.
# Note: gcc and clang use the same impl file so no need to switch between the
# two if switching between the two as CC
ATOMIC_OPERATIONS_IMPL=gcc
