# Warning flags
CFLAGS += -Wformat-overflow=2 -Wformat-truncation=2 -Wtrampolines
CFLAGS += -Warray-bounds=2 -Wimplicit-fallthrough=3 -Wlogical-op
CFLAGS += -Wshift-overflow=2 -Wstringop-overflow=4 -Warith-conversion
CFLAGS += -Wduplicated-cond -Wduplicated-branches -Wstack-usage=10000
CFLAGS += -Wcast-align=strict

ifeq ($(BUILD_TYPE),debug)
CFLAGS += -fsanitize=bounds-strict -fanalyzer
else ifeq ($(BUILD_TYPE),reldebug)
CFLAGS += -fsanitize=bounds-strict -fanalyzer
else ifeq ($(BUILD_TYPE),asan)
CFLAGS += -fsanitize=pointer-compare -fsanitize=pointer-subtract
endif
