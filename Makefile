include config.mk
include common.mk
include test/test.mk

C_WARNING_FLAGS = -Werror -Wall -Wextra -Wno-unused -Wfloat-equal \
		  -Wdouble-promotion -Wformat-overflow -Wformat=2 \
		  -Wnull-dereference -Wmissing-include-dirs -Wswitch-default \
		  -Wswitch-enum
C_PEDANTIC_FLAGS = -Wpedantic
C_FLAGS = -I. -O2 -std=c89 $(C_WARNING_FLAGS) $(C_CONFIG_FLAGS)

BIN_DIR_TARGETS = bin bin/test bin/test/api
BUILD_DIR_TARGETS = build build/queue build/test build/test/queue build/test/api

all: lib

lib: $(BIN_DIR_TARGETS) $(BUILD_DIR_TARGETS) $(OBJ_OUT)
	ar rcs bin/lib$(TARGET).a $(OBJ_OUT)

lib_so: C_FLAGS+=-fpic
lib_so: clean $(BIN_DIR_TARGETS) $(BUILD_DIR_TARGETS) $(OBJ_OUT)
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

build/test/api/%.o: C_FLAGS+=$(C_PEDANTIC_FLAGS)
build/test/api/%.o: test/api/%.c
	$(CC) $(C_FLAGS) -c $< -o $@

# This code is external and has warnings so I will not use those flags here
build/test/unity.o: test/unity.c
	$(CC) -I. -O2 -std=c89 -c $< -o $@

bin:
	@mkdir $@

bin/test:
	@mkdir $@

bin/test/api:
	@mkdir $@

build:
	@mkdir $@

build/queue:
	@mkdir $@

build/test:
	@mkdir $@

build/test/queue:
	@mkdir $@

build/test/api:
	@mkdir $@

clean:
	rm -rf build bin

clean_tests:
	rm $(TEST_BUILD_DIR)/**/*.test

clean_test_api_%:
	rm -f $(TEST_API_BUILD_DIR)/$*.test

test_summary:
	@python3 test/unity_test_summary.py ./build/test/

test_api_fiber_capability_get: clean_test_api_test_api_fiber_capability_get $(BIN_DIR_TARGETS) \
	$(BUILD_DIR_TARGETS) $(TEST_API_FIBER_CAPABILITY_GET_DEPS)

	$(CC) $(C_FLAGS) -o $(TEST_API_BIN_DIR)/$@ $(TEST_API_FIBER_CAPABILITY_GET_DEPS)
	@printf "\n"
	@$(TEST_API_BIN_DIR)/$@ | tee $(TEST_API_BUILD_DIR)/$@.test

.PHONY: all lib lib_so example clean clean_tests clean_test_api_% test_api_fiber_capability_get
