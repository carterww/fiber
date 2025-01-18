CC = gcc
TARGET = fiber
TESTFLAGS = -Itests -g
CFLAGS = -I. -O2 -std=c11

OBJ = fiber.o queue/fifo.o
OBJ_OUT = $(patsubst %, build/%, $(OBJ))

all: lib

lib: bin build $(OBJ_OUT)
	ar rcs bin/lib$(TARGET).a $(OBJ_OUT)

lib_so: CFLAGS+=-fpic
lib_so: bin build $(OBJ_OUT)
	$(CC) -shared -o bin/lib$(TARGET).so $(OBJ_OUT)

example: lib build/example.o
	$(CC) $(CFLAGS) -Lbin $(word 2,$^) -o bin/$@ -l:lib$(TARGET).a

testall: test_fifo test_thread_ll test_thread_alter test_fiber_init

test_fifo: bin build build/tests/queue/fifo.o build/fiber.o 
	$(CC) $(CFLAGS) $(word 3,$^) $(word 4,$^) -o bin/tests/$@
	bin/tests/$@

test_fiber_init: bin build build/tests/fiber_init.o build/queue/fifo.o
	$(CC) $(CFLAGS) $(word 3,$^) $(word 4,$^) -o bin/tests/$@
	bin/tests/$@

test_thread_alter: bin build build/tests/fiber_thread_alter.o build/queue/fifo.o
	$(CC) $(CFLAGS) $(word 3,$^) $(word 4,$^) -o bin/tests/$@
	bin/tests/$@

test_thread_ll: bin build build/tests/fiber_thread_ll.o build/queue/fifo.o
	$(CC) $(CFLAGS) $(word 3,$^) $(word 4,$^) -o bin/tests/$@
	bin/tests/$@

build/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

build/queue/%.o: src/queue/%.c
	$(CC) $(CFLAGS) -c $< -o $@

build/tests/%.o: tests/%.c
	$(CC) $(CFLAGS) -c $< -o $@

build/tests/queue/%.o: tests/queue/%.c
	$(CC) $(CFLAGS) -c $< -o $@

bin:
	@mkdir -p bin/tests

build:
	@mkdir -p build/queue
	@mkdir -p build/tests/queue

clean:
	rm -rf build bin

test_%: CFLAGS+=$(TESTFLAGS)
.PHONY: example clean so lib lib_so test_fifo test_fiber_init test_thread_alter test_thread_ll
