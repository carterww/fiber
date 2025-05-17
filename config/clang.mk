# Warning flags
CFLAGS += -Warray-bounds -Warray-bounds-pointer-arithmetic -Wassign-enum
CFLAGS += -Wbad-function-cast -Wconditional-uninitialized -Wformat-type-confusion
CFLAGS += -Widiomatic-parentheses -Wimplicit-fallthrough -Wloop-analysis
CFLAGS += -Wpointer-arith -Wshift-sign-overflow -Wshorten-64-to-32
CFLAGS += -Wtautological-constant-in-range-compare -Wunreachable-code-aggressive
CFLAGS += -Wthread-safety -Wthread-safety-beta -Wcomma

# Security flags
ifneq ($(BUILD_TYPE),asan)
CFLAGS += -fsanitize=safe-stack
LDFLAGS += -fsanitize=safe-stack
endif
