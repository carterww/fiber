CC = gcc
CFLAGS = -I. -Iqueue_impls -Wall

OBJ = fiber.o queue_impls/fifo_job_queue.o
OBJ_OUT = $(patsubst %, build/%, $(OBJ))

example: build_dir bin_dir example.o $(OBJ)
	$(CC) $(CFLAGS) $(OBJ_OUT) build/$(word 3,$^) -o bin/$@

test_fifo: build_test_dir test_bin_dir tests/queue_impls/test_fifo_job_queue.o $(OBJ)
	$(CC) $(CFLAGS) $(OBJ_OUT) build/$(word 3,$^) -o bin/tests/$@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o build/$@

build_dir:
	@mkdir -p build/queue_impls

build_test_dir: build_dir
	@mkdir -p build/tests/queue_impls

bin_dir:
	@mkdir -p bin

test_bin_dir: bin_dir
	@mkdir -p bin/tests

clean:
	rm -rf build/* bin/*

.PHONY: example test_fifo build_dir build_test_dir bin_dir test_bin_dir clean
