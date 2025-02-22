# Boring options
CC = cc
TARGET = fiber
# This can be one of the following:
# norm: Default build that should be used for production.
# debug: Debug build that has symbols built in.
# test: Special build that builds some code only used when testing.
ENV=norm

# Compile time options that the user can configure. For now, there are two types:
# booleans: These should be represented as 0 or 1
# enum: These should be represented as one of the values commented above it.

# boolean
# Compile fiber_assert statements
COMPILE_ASSERTS=1

# boolean
# Compile the default FIFO queue (fiber_fifo.h and src/queue/fifo.c)
COMPILE_FIBER_FIFO_QUEUE=1

# boolean
# Compile checks for Job ID overflow. This is recommended if sizeof(jid) < 8 bytes
COMPILE_CHECK_JID_OVERFLOW=1

# enum { pthread }
# Underlying threading library to use
THREADING_LIB=pthread

# enum { gcc | clang }
# Atomic operation implementation to use (currently only src/atomic_gcc_clang.c)
# Note: gcc and clang use the same impl file so no need to switch between the
# two if switching between the two as CC
ATOMIC_OPERATIONS_IMPL=gcc
