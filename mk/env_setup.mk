ifeq (,$(wildcard config.mk))
$(error Please copy config.def.mk to config.mk before running any make targets. \
	This separation ensures your config isn't tracked into git.)
endif

C_CONFIG_FLAGS = -D"FIBER_COMPILE_ASSERTS=($(COMPILE_ASSERTS))" \
		 -D"FIBER_COMPILE_FIBER_FIFO_QUEUE=($(COMPILE_FIBER_FIFO_QUEUE))"

QUEUE_OBJS =

FIBER_LOCK_MUTEX_INTERCEPT = 0
FIBER_LOCK_SEMAPHORE_INTERCEPT = 0

ifeq ($(ENV),norm)
C_CONFIG_FLAGS+=-D"FIBER_BUILD_ENV_NORM"
else ifeq ($(ENV),debug)
C_CONFIG_FLAGS+=-D"FIBER_BUILD_ENV_DEBUG" -g
else ifeq ($(ENV),test)
C_CONFIG_FLAGS+=-D"FIBER_BUILD_ENV_TEST" -g
C_CONFIG_FLAGS+=-D"FIBER_THREADING_INTERCEPT" -D"FIBER_LOCK_MUTEX_INTERCEPT" \
		-D"FIBER_LOCK_SEMAPHORE_INTERCEPT"
FIBER_LOCK_MUTEX_INTERCEPT = 1
FIBER_LOCK_SEMAPHORE_INTERCEPT = 1
else
$(error ENV was invalid.)
endif

ifeq ($(COMPILE_FIBER_FIFO_QUEUE),1)
QUEUE_OBJS+=queue/fifo.o
endif

ifeq ($(THREAD_IMPL),posix)
THREADING_OBJ=threading_pthread.o
C_CONFIG_FLAGS+=-D"FIBER_THREADING_LIB_PTHREAD"
else
$(error THREAD_IMPL in config.mk was invalid)
endif

# Add flags for fiber_lock
ifeq ($(MUTEX_IMPL),posix)
C_CONFIG_FLAGS+=-D"FIBER_LOCK_MUTEX_POSIX"
POSIX_C_SOURCE_REQUIREMENT=199506L
else
$(error MUTEX_IMPL was invalid. Valid options: posix.)
endif
ifeq ($(SEMAPHORE_IMPL),posix)
C_CONFIG_FLAGS+=-D"FIBER_LOCK_SEMAPHORE_POSIX"
else
$(error SEMAPHORE_IMPL was invalid. Valid options: posix.)
endif
ifeq ($(SPINLOCK_IMPL),posix)
C_CONFIG_FLAGS+=-D"FIBER_LOCK_SPIN_POSIX"
else
$(error SPINLOCK_IMPL was invalid. Valid options: posix.)
endif
ifeq ($(FUTEX_IMPL),linux)
C_CONFIG_FLAGS+=-D"FIBER_LOCK_FUTEX_LINUX"
else
$(error FUTEX_IMPL was invalid. Valid options: linux.)
endif

ifeq ($(ATOMIC_OPERATIONS_IMPL),gcc)
C_CONFIG_FLAGS+=-D"FIBER_ATOMIC_OPERATIONS_IMPL_GCC"
else ifeq ($(ATOMIC_OPERATIONS_IMPL),clang)
C_CONFIG_FLAGS+=-D"FIBER_ATOMIC_OPERATIONS_IMPL_CLANG"
else
$(error ATOMIC_OPERATIONS_IMPL in config.mk was invalid)
endif

OBJ = fiber.o thread_list.o version.o worker.o capability.o wait.o $(QUEUE_OBJS) $(THREADING_OBJ)
OBJ_OUT = $(patsubst %, build/%, $(OBJ))

Q = @
cc_cmd_generic_source = $(Q)$(CC) $(C_FLAGS) -c $< -o $@
test_summary_cmd = @python3 test/unity_test_summary.py ./build/test/result/
cc_pretty_print  = @printf "CC $<\n"

ifneq ($(Q),@)
cc_pretty_print =
endif
