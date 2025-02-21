include config.mk

# This can be one of the following:
# norm: Default build that should be used for production.
# debug: Debug build that has symbols built in.
# test: Special build that builds some code only used when testing.
ENV=norm

C_WARNING_FLAGS = -Werror -Wall -Wextra -Wno-unused -Wfloat-equal \
		  -Wdouble-promotion -Wformat-overflow -Wformat=2 \
		  -Wnull-dereference -Wmissing-include-dirs -Wswitch-default \
		  -Wswitch-enum
C_PEDANTIC_FLAGS = -Wpedantic
C_CONFIG_FLAGS = -D"FIBER_COMPILE_ASSERTS=($(COMPILE_ASSERTS))" \
		 -D"FIBER_COMPILE_CHECK_JID_OVERFLOW=($(COMPILE_CHECK_JID_OVERFLOW))" \
		 -D"FIBER_COMPILE_FIBER_FIFO_QUEUE=($(COMPILE_FIBER_FIFO_QUEUE))"

OBJ = fiber.o thread_list.o version.o worker.o

ifeq ($(ENV),norm)
	C_CONFIG_FLAGS+=-D"FIBER_BUILD_ENV_NORM"
else ifeq ($(ENV),debug)
	C_CONFIG_FLAGS+=-D"FIBER_BUILD_ENV_DEBUG"
else ifeq ($(ENV),test)
	C_CONFIG_FLAGS+=-D"FIBER_BUILD_ENV_TEST"
else
	$(error ENV was invalid.)
endif

ifeq ($(COMPILE_FIBER_FIFO_QUEUE),1)
	OBJ+=queue/fifo.o
endif

ifeq ($(THREADING_LIB),pthread)
	OBJ+=threading_pthread.o
	C_CONFIG_FLAGS+=-D"FIBER_THREADING_LIB_PTHREAD"
else
	$(error THREADING_LIB in config.mk was invalid)
endif

ifeq ($(ATOMIC_OPERATIONS_IMPL),gcc)
	OBJ+=atomic_gcc_clang.o
	C_CONFIG_FLAGS+=-D"FIBER_ATOMIC_OPERATIONS_IMPL_GCC"
else ifeq ($(ATOMIC_OPERATIONS_IMPL),clang)
	OBJ+=atomic_gcc_clang.o
	C_CONFIG_FLAGS+=-D"FIBER_ATOMIC_OPERATIONS_IMPL_CLANG"
else
	$(error ATOMIC_OPERATIONS_IMPL in config.mk was invalid)
endif

OBJ_OUT = $(patsubst %, build/%, $(OBJ))

C_FLAGS = -I. -O2 -std=c89 $(C_WARNING_FLAGS) $(C_CONFIG_FLAGS)

all: lib

lib: bin build $(OBJ_OUT)
	ar rcs bin/lib$(TARGET).a $(OBJ_OUT)

lib_so: C_FLAGS+=-fpic
lib_so: clean bin build $(OBJ_OUT)
	$(CC) -shared -o bin/lib$(TARGET).so $(OBJ_OUT)

example: lib build/example.o
	$(CC) $(C_FLAGS) -Lbin $(word 2,$^) -o bin/$@ -l:lib$(TARGET).a

build/threading_pthread.o: src/threading_pthread.c
	$(CC) $(C_FLAGS) -pthread -c $< -o $@

build/queue/%.o: C_FLAGS+=$(C_PEDANTIC_FLAGS)
build/queue/%.o: src/queue/%.c
	$(CC) $(C_FLAGS) -c $< -o $@

build/%.o: C_FLAGS+=$(C_PEDANTIC_FLAGS)
build/%.o: src/%.c
	$(CC) $(C_FLAGS) -c $< -o $@

bin:
	@mkdir -p bin/tests

build:
	@mkdir -p build/queue
	@mkdir -p build/tests/queue

clean:
	rm -rf build bin

.PHONY: all lib lib_so example bin build clean
