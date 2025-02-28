# This file defines all the dependencies for each test suite. Each suite will declare their
# own dependencies (object files) so modules can be mocked/swapped easily.

TEST_BIN_DIR = bin/test
TEST_API_BIN_DIR = $(TEST_BIN_DIR)/api
TEST_QUEUE_BIN_DIR = $(TEST_BIN_DIR)/queue
TEST_QUEUE_FIFO_BIN_DIR = $(TEST_BIN_DIR)/queue/fifo

TEST_BIN_DIRS = $(TEST_BIN_DIR) $(TEST_API_BIN_DIR) $(TEST_QUEUE_BIN_DIR) \
		$(TEST_QUEUE_FIFO_BIN_DIR)


TEST_BUILD_DIR = build/test
TEST_API_BUILD_DIR = $(TEST_BUILD_DIR)/api
TEST_QUEUE_BUILD_DIR = $(TEST_BUILD_DIR)/queue
TEST_QUEUE_FIFO_BUILD_DIR = $(TEST_BUILD_DIR)/queue/fifo
TEST_MOCK_BUILD_DIR = $(TEST_BUILD_DIR)/mock
TEST_MOCK_ALLOC_BUILD_DIR = $(TEST_BUILD_DIR)/mock/alloc
TEST_MOCK_THREADING_BUILD_DIR = $(TEST_BUILD_DIR)/mock/threading

TEST_BUILD_DIRS = $(TEST_BUILD_DIR) $(TEST_API_BUILD_DIR) $(TEST_QUEUE_BUILD_DIR) \
		  $(TEST_QUEUE_FIFO_BUILD_DIR) $(TEST_MOCK_BUILD_DIR) \
		  $(TEST_MOCK_ALLOC_BUILD_DIR) $(TEST_MOCK_THREADING_BUILD_DIR)

TEST_ALL_DEPS = build/test/unity.o
TEST_COMMON_DEPS = $(OBJ_OUT)

# List of TESTS
TEST_QUEUE_FIFO_INIT = $(TEST_QUEUE_FIFO_BIN_DIR)/test_fifo_init
TEST_QUEUE_FIFO_INIT_MALLOC_ERROR = $(TEST_QUEUE_FIFO_BIN_DIR)/test_fifo_init_malloc_error
TEST_QUEUE_FIFO_INIT_MUTEX_ERROR = $(TEST_QUEUE_FIFO_BIN_DIR)/test_fifo_init_mutex_error
TEST_QUEUE_FIFO_INIT_SEM_ERROR = $(TEST_QUEUE_FIFO_BIN_DIR)/test_fifo_init_sem_error

TEST_API_CAPABILITY = $(TEST_API_BIN_DIR)/test_fiber_capability_get
TEST_API_INIT = $(TEST_API_BIN_DIR)/test_fiber_api_init
TEST_API_LIBVERSION_COMPAT = $(TEST_API_BIN_DIR)/test_fiber_api_libversion_compatible

# Test queue dependencies
TEST_QUEUE_FIFO_COMMON_DEPS = $(TEST_ALL_DEPS) build/queue/fifo.o \
			      $(TEST_MOCK_THREADING_BUILD_DIR)/threading_trace_fault.o \
			      $(TEST_MOCK_ALLOC_BUILD_DIR)/alloc_trace.o \

$(TEST_QUEUE_FIFO_INIT)_DEPS = $(TEST_QUEUE_FIFO_COMMON_DEPS) $(TEST_QUEUE_FIFO_BUILD_DIR)/fifo_init.o

$(TEST_QUEUE_FIFO_INIT_MALLOC_ERROR)_DEPS = $(TEST_QUEUE_FIFO_COMMON_DEPS) \
	                             	    $(TEST_MOCK_ALLOC_BUILD_DIR)/alloc_fault.o \
				            $(TEST_QUEUE_FIFO_BUILD_DIR)/fifo_init_malloc_error.o

# $(TEST_QUEUE_FIFO_INIT_MUTEX_ERROR)_DEPS = $(filter-out build/$(THREADING_OBJ), $(TEST_QUEUE_FIFO_COMMON_DEPS)) \
# 	                                   $(TEST_MOCK_THREADING_BUILD_DIR)/threading_trace_fault.o \
# 	                                   $(TEST_QUEUE_BUILD_DIR)/fifo/fifo_init_mutex_error.o

$(TEST_QUEUE_FIFO_INIT_SEM_ERROR)_DEPS = $(TEST_QUEUE_FIFO_COMMON_DEPS) \
	                                 $(TEST_QUEUE_FIFO_BUILD_DIR)/fifo_init_sem_error.o

# Test API dependencies

$(TEST_API_CAPABILITY)_DEPS = $(TEST_ALL_DEPS) $(TEST_API_BUILD_DIR)/fiber_capability_get.o \
			      $(TEST_COMMON_DEPS)

$(TEST_API_INIT)_DEPS = $(TEST_ALL_DEPS) $(TEST_API_BUILD_DIR)/fiber_init.o \
			$(filter-out build/$(THREADING_OBJ), $(TEST_COMMON_DEPS)) \
			$(TEST_MOCK_ALLOC_BUILD_DIR)/alloc_trace.o \
			$(TEST_MOCK_THREADING_BUILD_DIR)/threading_trace_fault.o

$(TEST_API_LIBVERSION_COMPAT)_DEPS = $(TEST_ALL_DEPS) $(TEST_API_BUILD_DIR)/fiber_libversion_compatible.o \
				     $(filter-out %version.o, $(TEST_COMMON_DEPS))

TEST_QUEUE_ALL = $(TEST_QUEUE_FIFO_INIT) $(TEST_QUEUE_FIFO_INIT_MALLOC_ERROR) $(TEST_QUEUE_FIFO_INIT_SEM_ERROR)
TEST_API_ALL   = $(TEST_API_CAPABILITY) $(TEST_API_INIT) $(TEST_API_LIBVERSION_COMPAT)

TEST_ALL = $(TEST_QUEUE_ALL) $(TEST_API_ALL)
