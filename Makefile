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

# This code is external and has warnings so I will not use those flags here
build/test/unity.o: test/unity.c
	$(CC) -I. -O2 -std=c89 -c $< -o $@

build/queue/%.o: C_FLAGS+=$(C_PEDANTIC_FLAGS)
build/queue/%.o: src/queue/%.c
	$(CC) $(C_FLAGS) -c $< -o $@

build/%.o: C_FLAGS+=$(C_PEDANTIC_FLAGS)
build/%.o: src/%.c
	$(CC) $(C_FLAGS) -c $< -o $@

build/test/api/%.o: C_FLAGS+=$(C_PEDANTIC_FLAGS)
build/test/api/%.o: test/api/%.c
	$(CC) $(C_FLAGS) -c $< -o $@

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
	rm -f $(TEST_BUILD_DIR)/**/*.test

clean_test_api_%:
	@rm -f $(TEST_API_BUILD_DIR)/$*.test

test_summary:
	@python3 test/unity_test_summary.py ./build/test/

# Test groups
test_api: clean_tests $(TEST_API_CAPABILITY) $(TEST_API_LIBVERSION_COMPAT) test_summary

# Test API runners
$(TEST_API_CAPABILITY): clean_$(TEST_API_CAPABILITY) $(BIN_DIR_TARGETS) $(BUILD_DIR_TARGETS) \
	$(TEST_API_BIN_DIR)/$(TEST_API_CAPABILITY)

	@printf "\n"
	@$(TEST_API_BIN_DIR)/$@ | tee $(TEST_API_BUILD_DIR)/$@.test

$(TEST_API_LIBVERSION_COMPAT): clean_$(TEST_API_LIBVERSION_COMPAT) $(BIN_DIR_TARGETS) $(BUILD_DIR_TARGETS) \
	$(TEST_API_BIN_DIR)/$(TEST_API_LIBVERSION_COMPAT)

	@printf "\n"
	@$(TEST_API_BIN_DIR)/$@ | tee $(TEST_API_BUILD_DIR)/$@.test
	
# Test API builders
$(TEST_API_BIN_DIR)/$(TEST_API_LIBVERSION_COMPAT): $(TEST_API_FIBER_LIBVERSION_COMPATIBLE_DEPS)
	@$(CC) $(C_FLAGS) -o $@ $^

$(TEST_API_BIN_DIR)/$(TEST_API_CAPABILITY): $(TEST_API_FIBER_CAPABILITY_GET_DEPS)
	$(CC) $(C_FLAGS) -o $@ $^

.PHONY: all lib lib_so example clean clean_tests clean_test_api_% $(TEST_API_CAPABILITY) \
	$(TEST_API_LIBVERSION_COMPAT)
