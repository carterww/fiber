ifeq (,$(wildcard config.mk))
$(error Please copy config.def.mk to config.mk before running any make targets. \
	This separation ensures your config isn't tracked into git.)
endif

C_CONFIG_FLAGS = -D"FIBER_COMPILE_ASSERTS=($(COMPILE_ASSERTS))" \
		 -D"FIBER_COMPILE_FIBER_FIFO_QUEUE=($(COMPILE_FIBER_FIFO_QUEUE))"

QUEUE_OBJS =

ifeq ($(ENV),norm)
C_CONFIG_FLAGS+=-D"FIBER_BUILD_ENV_NORM"
else ifeq ($(ENV),debug)
C_CONFIG_FLAGS+=-D"FIBER_BUILD_ENV_DEBUG" -g
else ifeq ($(ENV),test)
C_CONFIG_FLAGS+=-D"FIBER_BUILD_ENV_TEST" -g
else
$(error ENV was invalid.)
endif

ifeq ($(COMPILE_FIBER_FIFO_QUEUE),1)
QUEUE_OBJS+=queue/fifo.o
endif

ifeq ($(THREADING_LIB),pthread)
THREADING_OBJ=threading_pthread.o
C_CONFIG_FLAGS+=-D"FIBER_THREADING_LIB_PTHREAD"
else
$(error THREADING_LIB in config.mk was invalid)
endif

ifeq ($(ATOMIC_OPERATIONS_IMPL),gcc)
C_CONFIG_FLAGS+=-D"FIBER_ATOMIC_OPERATIONS_IMPL_GCC"
else ifeq ($(ATOMIC_OPERATIONS_IMPL),clang)
C_CONFIG_FLAGS+=-D"FIBER_ATOMIC_OPERATIONS_IMPL_CLANG"
else
$(error ATOMIC_OPERATIONS_IMPL in config.mk was invalid)
endif

OBJ = fiber.o thread_list.o version.o worker.o capability.o $(QUEUE_OBJS) $(THREADING_OBJ)
OBJ_OUT = $(patsubst %, build/%, $(OBJ))

Q = @
cc_cmd_generic_source = $(Q)$(CC) $(C_FLAGS) -c $< -o $@
cc_cmd_generic_out    = $(Q)$(CC) $(C_FLAGS) $(LD_FLAGS) -o $@ $^
test_summary_cmd = @python3 test/unity_test_summary.py ./build/test/result/
cc_pretty_print  = @printf "CC $<\n"

ifneq ($(Q),@)
cc_pretty_print =
endif
