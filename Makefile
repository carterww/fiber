include config.mk
include common.mk
include test/test.mk

C_WARNING_FLAGS = -Werror -Wall -Wextra -Wno-unused -Wfloat-equal \
		  -Wdouble-promotion -Wformat-overflow -Wformat=2 \
		  -Wnull-dereference -Wmissing-include-dirs -Wswitch-default \
		  -Wswitch-enum
C_PEDANTIC_FLAGS = -Wpedantic
C_FLAGS = -I. -O2 -std=c89 $(C_WARNING_FLAGS) $(C_CONFIG_FLAGS)

Q = @

BIN_DIRS = bin \
	   bin/test \
	   bin/test/api
BUILD_DIRS = build \
	     build/queue build/test \
	     build/test/result build/test/queue build/test/api build/test/mock \
	     build/test/mock/alloc

DIRS = $(BIN_DIRS) $(BUILD_DIRS)

cc_cmd_generic_source = $(Q)$(CC) $(C_FLAGS) -c $< -o $@
cc_cmd_generic_out    = $(Q)$(CC) $(C_FLAGS) -o $@ $^
test_summary_cmd = @python3 test/unity_test_summary.py ./build/test/result/
cc_pretty_print  = @printf "CC $<\n"

ifneq ($(Q),@)
	cc_pretty_print =
endif

all: lib

lib: $(DIRS) $(OBJ_OUT)
	@ar rcs bin/lib$(TARGET).a $(OBJ_OUT)
	@printf "ar lib$(TARGET).a\n"

lib_so: C_FLAGS+=-fpic
lib_so: clean $(DIRS) $(OBJ_OUT)
	@$(CC) $(C_FLAGS) -shared -o bin/lib$(TARGET).so $(OBJ_OUT)
	@printf "CC -shared lib$(TARGET).so\n"

example: lib build/example.o
	$(Q)$(CC) $(C_FLAGS) -Lbin $(word 2,$^) -o bin/$@ -l:lib$(TARGET).a

# This code is external and has warnings so I will not use those flags here
build/test/unity.o: C_FLAGS:=$(filter-out $(C_WARNING_FLAGS) std=c89, $(C_FLAGS))
build/test/%.o: test/%.c
	$(cc_cmd_generic_source)
	$(cc_pretty_print)

build/threading_pthread.o: C_FLAGS+=-pthread
build/%.o: C_FLAGS+=$(C_PEDANTIC_FLAGS)
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
	@find bin/test -type f -executable -print0 | xargs -0 -P 8 -I {} sh -c '{} > build/test/result/$$RANDOM.test' \;
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

define TARGET_COMPILE_TEST
$(1): $$($(1)_DEPS)
	$$(cc_cmd_generic_out)
endef

$(foreach TEST_BIN,$(TEST_ALL),$(eval $(call TARGET_COMPILE_TEST,$(TEST_BIN))))

.PHONY: all lib lib_so example clean test_clean test_result_clean test_run test_api
