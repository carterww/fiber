include config.mk
include mk/env_setup.mk
include mk/flags.mk
include mk/test/test.mk

BIN_DIRS = bin $(TEST_BIN_DIRS)
BUILD_DIRS = build build/queue $(TEST_BUILD_DIRS) build/test/result
DIRS = lib $(BIN_DIRS) $(BUILD_DIRS)

FIBER_LOCK_LIB_STATIC_OUT = deps/fiber_lock/lib/libfiber_lock.a

all: lib_static

lib_static: lib/lib$(TARGET).a

lib_so: lib/lib$(TARGET).so

deps: $(FIBER_LOCK_LIB_STATIC_OUT)

lib/lib$(TARGET).a: lib $(BUILD_DIRS) $(OBJ_OUT)
	@ar rcs $@ $(OBJ_OUT)
	@printf "ar $@\n"

lib/lib$(TARGET).so: C_FLAGS := $(filter-out $(C_PIC_FLAG),$(C_FLAGS))
lib/lib$(TARGET).so: C_FLAGS += $(C_PIC_FLAG)
lib/lib$(TARGET).so: lib $(BUILD_DIRS) $(OBJ_OUT)
	@$(CC) $(C_FLAGS) $(LD_FLAGS) -shared -o $@ $(OBJ_OUT)
	@printf "CC -shared $@\n"

example: lib/lib$(TARGET).a $(FIBER_LOCK_LIB_STATIC_OUT) build/example.o $(BIN_DIRS)
	$(Q)$(CC) $(C_FLAGS) $(LD_FLAGS) $(word 3,$^) -L. -l:$(word 1,$^) \
		-l:$(FIBER_LOCK_LIB_STATIC_OUT) -o bin/$@

# This code is external and has warnings so I will not use those flags here
build/test/unity.o: C_FLAGS:=$(filter-out $(C_WARNING_FLAGS) std=c89, $(C_FLAGS))
build/test/%.o: test/%.c
	$(cc_cmd_generic_source)
	$(cc_pretty_print)

build/threading_pthread.o: C_FLAGS+=-pthread
build/%.o: src/%.c
	$(cc_cmd_generic_source)
	$(cc_pretty_print)

$(DIRS):
	@mkdir $@

$(FIBER_LOCK_LIB_STATIC_OUT): deps/fiber_lock/config.mk
	@$(MAKE) CC=$(CC) ENV=$(ENV) TARGET=fiber_lock DEBUG_SANITIZE=$(DEBUG_SANITIZE) \
		PIC=$(PIC) COMPILE_ASSERTS=$(COMPILE_ASSERTS) Q=$(Q)\
		ATOMIC_OPERATIONS_IMPL=$(ATOMIC_OPERATIONS_IMPL) \
		MUTEX_IMPL=$(MUTEX_IMPL) SEMAPHORE_IMPL=$(SEMAPHORE_IMPL) \
		SPINLOCK_IMPL=$(SPINLOCK_IMPL) FUTEX_IMPL=$(FUTEX_IMPL) \
		FIBER_LOCK_MUTEX_INTERCEPT=$(FIBER_LOCK_MUTEX_INTERCEPT) \
		FIBER_LOCK_SEMAPHORE_INTERCEPT=$(FIBER_LOCK_SEMAPHORE_INTERCEPT) \
		-C deps/fiber_lock lib_static

deps/fiber_lock/config.mk:
	@cp deps/fiber_lock/config.def.mk deps/fiber_lock/config.mk

clean:
	@find build -type f -exec rm {} +
	@find bin -type f -exec rm {} +
	@find lib -type f -exec rm {} +
	@$(MAKE) -C deps/fiber_lock clean

test_clean:
	@find build/test -type f -exec rm {} +
	@find bin/test -type f -exec rm {} +

test_result_clean:
	@find build/test/result -type f -exec rm {} +

test_run: test_result_clean $(DIRS)
	@find bin/test -type f -executable -print0 | \
		xargs -0 -P 8 -I {} \
		sh -c '{} > build/test/result/$$RANDOM.test' \;
	@wait
	$(test_summary_cmd)

test_run_verbose: test_result_clean $(DIRS)
	@find bin/test -type f -executable -print0 | \
		xargs -0 -P 8 -I {} \
		sh -c '{} | tee build/test/result/$$RANDOM.test ; echo ""' \;
	@wait
	$(test_summary_cmd)

# Test groups
test_api: $(DIRS) $(TEST_API_ALL)

test_queue: $(DIRS) $(TEST_QUEUE_ALL)

test_all: $(DIRS) $(TEST_ALL)

define TARGET_COMPILE_TEST
$(1): $$(FIBER_LOCK_LIB_STATIC_OUT) $$($(1)_DEPS)
	$$(Q)$$(CC) -fno-plt $$(C_FLAGS) $$(LD_FLAGS) $$($(1)_DEPS) -L. -l:$$< -o $$@
endef

$(foreach TEST_BIN,$(TEST_ALL),$(eval $(call TARGET_COMPILE_TEST,$(TEST_BIN))))

.PHONY: all lib_static lib_so deps example clean test_clean test_result_clean test_run test_api
