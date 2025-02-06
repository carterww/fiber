CC = cc
TARGET = fiber

C_WARNING_FLAGS = -Werror -Wall -Wextra -Wno-unused -Wfloat-equal -Wdouble-promotion -Wformat-overflow -Wformat=2 -Wnull-dereference -Wmissing-include-dirs -Wswitch-default -Wswitch-enum
C_PEDANTIC_FLAGS = -Wpedantic

C_FLAGS = -I. -O2 -std=c89 $(C_WARNING_FLAGS)
C_TEST_FLAGS = -I. -Itests -g -O2 -std=c11 $(C_WARNING_FLAGS)

OBJ = fiber.o
OBJ_OUT = $(patsubst %, build/%, $(OBJ))

all: lib

lib: bin build $(OBJ_OUT)
	ar rcs bin/lib$(TARGET).a $(OBJ_OUT)

lib_so: C_FLAGS+=-fpic
lib_so: clean bin build $(OBJ_OUT)
	$(CC) -shared -o bin/lib$(TARGET).so $(OBJ_OUT)

example: lib build/example.o
	$(CC) $(C_FLAGS) -Lbin $(word 2,$^) -o bin/$@ -l:lib$(TARGET).a

testall: test_fifo test_thread_ll test_thread_alter test_fiber_init

test_fifo: bin build build/tests/queue/fifo.o build/fiber.o 
	$(CC) $(C_TEST_FLAGS) $(word 3,$^) $(word 4,$^) -o bin/tests/$@
	bin/tests/$@

test_fiber_init: bin build build/tests/fiber_init.o build/queue/fifo.o
	$(CC) $(C_TEST_FLAGS) $(word 3,$^) $(word 4,$^) -o bin/tests/$@
	bin/tests/$@

test_thread_alter: bin build build/tests/fiber_thread_alter.o build/queue/fifo.o
	$(CC) $(C_TEST_FLAGS) $(word 3,$^) $(word 4,$^) -o bin/tests/$@
	bin/tests/$@

test_thread_ll: bin build build/tests/fiber_thread_ll.o build/queue/fifo.o
	$(CC) $(C_TEST_FLAGS) $(word 3,$^) $(word 4,$^) -o bin/tests/$@
	bin/tests/$@

build/queue/%.o: C_FLAGS+=$(C_PEDANTIC_FLAGS)
build/queue/%.o: src/queue/%.c
	$(CC) $(C_FLAGS) -c $< -o $@

build/%.o: C_FLAGS+=$(C_PEDANTIC_FLAGS)
build/%.o: src/%.c
	$(CC) $(C_FLAGS) -c $< -o $@

build/tests/%.o: tests/%.c
	$(CC) $(C_TEST_FLAGS) -c $< -o $@

build/tests/queue/%.o: tests/queue/%.c
	$(CC) $(C_TEST_FLAGS) -c $< -o $@

bin:
	@mkdir -p bin/tests

build:
	@mkdir -p build/queue
	@mkdir -p build/tests/queue

clean:
	rm -rf build bin

.PHONY: example clean so lib lib_so test_fifo test_fiber_init test_thread_alter test_thread_ll
