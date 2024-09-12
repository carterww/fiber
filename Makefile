CC = gcc
CFLAGS = -I. -Iqueue_impls -O2
TESTFLAGS = -Itests -g

OBJ = fiber.o queue_impls/fifo_job_queue.o
OBJ_OUT = $(patsubst %, build/%, $(OBJ))

DEFS = -DFIBER_ASSERTS -DFIBER_CHECK_JID_OVERFLOW

example: build_dir bin_dir example.o $(OBJ)
	$(CC) $(CFLAGS) $(OBJ_OUT) build/$(word 3,$^) -o bin/$@

test_%: CFLAGS+=$(TESTFLAGS)
test_all: test_fifo test_thread_ll test_fiber_init

test_fifo: test_dirs tests/queue_impls/test_fifo_job_queue.o $(OBJ)
	$(CC) $(CFLAGS) $(OBJ_OUT) build/$(word 2,$^) -o bin/tests/$@
	bin/tests/$@

test_fiber_init: test_dirs tests/fiber_init.o queue_impls/fifo_job_queue.o
	$(CC) $(CFLAGS) $(DEFS) build/$(word 2,$^) build/$(word 3,$^) -o bin/tests/$@
	bin/tests/$@

test_thread_alter: test_dirs tests/fiber_thread_alter.o queue_impls/fifo_job_queue.o
	$(CC) $(CFLAGS) $(DEFS) build/$(word 2,$^) build/$(word 3,$^) -o bin/tests/$@
	bin/tests/$@

test_thread_ll: test_dirs tests/fiber_thread_ll.o queue_impls/fifo_job_queue.o
	$(CC) $(CFLAGS) $(DEFS) build/$(word 2,$^) build/$(word 3,$^) -o bin/tests/$@
	bin/tests/$@

%.o: %.c
	$(CC) $(CFLAGS) $(DEFS) -c $< -o build/$@

build_dir:
	@mkdir -p build/queue_impls

build_test_dir: build_dir
	@mkdir -p build/tests/queue_impls

bin_dir:
	@mkdir -p bin

test_bin_dir: bin_dir
	@mkdir -p bin/tests

test_dirs: build_test_dir test_bin_dir

clean:
	rm -rf build/* bin/*

.PHONY: example build_dir bin_dir test_dirs clean
