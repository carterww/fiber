CC = gcc
TARGET = fiber
FASTFLAGS = -march=native -mtune=native
TESTFLAGS = -Itests -g
CFLAGS = $(FASTFLAGS) -I. -Iqueue_impls -O2 -std=c11

OBJ = fiber.o queue_impls/fifo_job_queue.o
OBJ_OUT = $(patsubst %, build/%, $(OBJ))

DEFS = -DFIBER_ASSERTS

all: lib

lib: build_dir lib_dir $(OBJ)
	ar rcs lib/lib$(TARGET).a $(OBJ_OUT)

lib_so: CFLAGS+=-fpic
lib_so: build_dir lib_dir $(OBJ)
	$(CC) -shared -o lib/lib$(TARGET).so $(OBJ_OUT)

example: build_dir bin_dir example.o lib
	$(CC) $(CFLAGS) -Llib build/$(word 3,$^) -o bin/$@ -l:lib$(TARGET).a

testall: test_fifo test_thread_ll test_thread_alter test_fiber_init

test_fifo: dirs_test tests/queue_impls/test_fifo_job_queue.o $(OBJ)
	$(CC) $(CFLAGS) $(OBJ_OUT) build/$(word 2,$^) -o bin/tests/$@
	bin/tests/$@

test_fiber_init: dirs_test tests/fiber_init.o queue_impls/fifo_job_queue.o
	$(CC) $(CFLAGS) $(DEFS) build/$(word 2,$^) build/$(word 3,$^) -o bin/tests/$@
	bin/tests/$@

test_thread_alter: dirs_test tests/fiber_thread_alter.o queue_impls/fifo_job_queue.o
	$(CC) $(CFLAGS) $(DEFS) build/$(word 2,$^) build/$(word 3,$^) -o bin/tests/$@
	bin/tests/$@

test_thread_ll: dirs_test tests/fiber_thread_ll.o queue_impls/fifo_job_queue.o
	$(CC) $(CFLAGS) $(DEFS) build/$(word 2,$^) build/$(word 3,$^) -o bin/tests/$@
	bin/tests/$@

%.o: %.c
	$(CC) $(CFLAGS) $(DEFS) -c $< -o build/$@

build_dir:
	@mkdir -p build/queue_impls

build_dir_test: build_dir
	@mkdir -p build/tests/queue_impls

bin_dir:
	@mkdir -p bin

lib_dir:
	@mkdir -p lib

bin_dir_test: bin_dir
	@mkdir -p bin/tests

dirs_test: build_dir_test bin_dir_test

clean:
	rm -rf build/* bin/* lib/*

test_%: CFLAGS+=$(TESTFLAGS)
.PHONY: example build_dir bin_dir dirs_test clean so lib lib_dir
