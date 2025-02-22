# This file defines all the dependencies for each test suite. Each suite will declare their
# own dependencies (object files) so modules can be mocked/swapped easily.

TEST_BIN_DIR = bin/test
TEST_BUILD_DIR = build/test
TEST_API_BIN_DIR = $(TEST_BIN_DIR)/api
TEST_API_BUILD_DIR = $(TEST_BUILD_DIR)/api
TEST_ALL_DEPS = build/test/unity.o
TEST_COMMON_DEPS = $(OBJ_OUT)

# Test dependencies
TEST_API_FIBER_CAPABILITY_GET_DEPS = $(TEST_ALL_DEPS) $(TEST_API_BUILD_DIR)/fiber_capability_get.o \
				     $(OBJ_OUT)
