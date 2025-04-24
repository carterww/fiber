include config.mk
include mk/env_setup.mk
include mk/flags.mk
include mk/test/test.mk

BIN_DIRS = bin $(TEST_BIN_DIRS)
BUILD_DIRS = build build/queue $(TEST_BUILD_DIRS) build/test/result
DIRS = $(BIN_DIRS) $(BUILD_DIRS)

all: lib

lib: lib$(TARGET).a

lib_standalone: lib$(TARGET)_standalone.a

lib_so: lib$(TARGET).so

lib$(TARGET).a: $(DIRS) $(OBJ_OUT)
	@ar rcs bin/$@ $(OBJ_OUT)
	@printf "ar $@\n"

lib$(TARGET)_standalone.a: $(DIRS) $(OBJ_OUT)
	@ar rcs bin/$@ $(OBJ_OUT)
	@printf "ar $@\n"

lib$(TARGET).so: C_FLAGS := $(filter-out $(C_PIC_FLAG),$(C_FLAGS))
lib$(TARGET).so: C_FLAGS += $(C_PIC_FLAG)
lib$(TARGET).so: $(DIRS) $(OBJ_OUT)
	@$(CC) $(C_FLAGS) $(LD_FLAGS) -shared -o bin/$@ $(OBJ_OUT)
	@printf "CC -shared $@\n"

example: lib build/example.o
	$(Q)$(CC) $(C_FLAGS) $(LD_FLAGS) -Lbin $(word 2,$^) -o bin/$@ -l:lib$(TARGET).a

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

clean:
	@find build -type f -exec rm {} +
	@find bin -type f -exec rm {} +

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
$(1): $$($(1)_DEPS)
	$$(cc_cmd_generic_out)
endef

$(foreach TEST_BIN,$(TEST_ALL),$(eval $(call TARGET_COMPILE_TEST,$(TEST_BIN))))

.PHONY: all lib lib_standalone lib_so example clean test_clean test_result_clean test_run test_api
