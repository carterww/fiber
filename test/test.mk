# This file defines all the dependencies for each test suite. Each suite will declare their
# own dependencies (object files) so modules can be mocked/swapped easily.

TEST_BIN_DIR = bin/test
TEST_BUILD_DIR = build/test

TEST_MOCK_DIR = test/mock

TEST_API_BIN_DIR = $(TEST_BIN_DIR)/api
TEST_API_BUILD_DIR = $(TEST_BUILD_DIR)/api

TEST_MOCK_BUILD_DIR = $(TEST_BUILD_DIR)/mock

TEST_ALL_DEPS = build/test/unity.o
TEST_COMMON_DEPS = $(OBJ_OUT)

# List of TESTS
TEST_NAME_PREFIX = test_fiber
TEST_API_NAME_PREFIX = test_api_fiber

TEST_API_CAPABILITY = $(TEST_API_NAME_PREFIX)_capability_get
TEST_API_INIT = $(TEST_API_NAME_PREFIX)_init
TEST_API_LIBVERSION_COMPAT = $(TEST_API_NAME_PREFIX)_libversion_compatible

# Test API dependencies

TEST_API_FIBER_CAPABILITY_GET_DEPS = $(TEST_ALL_DEPS) $(TEST_API_BUILD_DIR)/fiber_capability_get.o \
				     $(TEST_COMMON_DEPS)
# This suite uses a mock job queue so we don't need any queue modules. The mock queue is implemented in
# a header.
# The underlying thread library also isn't used because we don't need to actually start threads for these
# tests.
TEST_API_FIBER_INIT_DEPS = $(TEST_ALL_DEPS) $(TEST_API_BUILD_DIR)/fiber_init.o \
			   $(TEST_MOCK_BUILD_DIR)/threading/threading_noop.o \
			   $(filter-out build/queue/%.o build/threading_%.o, $(TEST_COMMON_DEPS))
# This test suite implements its own fiber_libversion function so src/version.c is not
# needed.
TEST_API_FIBER_LIBVERSION_COMPATIBLE_DEPS = $(TEST_ALL_DEPS) $(TEST_API_BUILD_DIR)/fiber_libversion_compatible.o \
					    $(filter-out %version.o, $(TEST_COMMON_DEPS))
