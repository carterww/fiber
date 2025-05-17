# Build time options
COMPILE_ASSERTS ?= 1

FIBER_VERSION_MAJOR := 0
FIBER_VERSION_MINOR := 2
FIBER_VERSION_PATCH := 4

CC ?= clang
LD := $(CC)

DESTDIR ?=
PREFIX ?= $(DESTDIR)/usr/local
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include

FIBER_PATH := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
SRC_DIR := $(FIBER_PATH)/src
BUILD_DIR := $(FIBER_PATH)/build
OBJ_DIR := $(BUILD_DIR)/objs

STATIC_LIB := $(BUILD_DIR)/libfiber.a
SHARED_LIB := $(BUILD_DIR)/libfiber.so
ALL_LIBS := $(STATIC_LIB) $(SHARED_LIB)

LDLIBS += -lck

CFLAGS += -std=c99 -fPIC -pthread -I$(FIBER_PATH)/include -I$(SRC_DIR) -I/usr/local/include
CFLAGS += -MMD -MP

# Warning flags
CFLAGS += -Werror -Wall -Wextra -Wpedantic -Wno-unused -Wfloat-equal
CFLAGS += -Wdouble-promotion -Wformat=2 -Wformat-security -Wstack-protector
CFLAGS += -Walloca -Wvla -Wcast-qual -Wconversion -Wformat-signedness -Wshadow
CFLAGS += -Wstrict-overflow=4 -Wundef -Wstrict-prototypes -Wswitch-default
CFLAGS += -Wswitch-enum -Wnull-dereference -Wmissing-include-dirs

# Security flags
CFLAGS += -fstack-protector-strong -fstack-clash-protection -fsanitize=bounds
CFLAGS += -fsanitize-undefined-trap-on-error -fvisibility=hidden

LDFLAGS += -fPIC -pthread
LDFLAGS += -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -Wl,-z,separate-code

# release, debug, reldebug, or asan
BUILD_TYPE ?= release

ifeq ($(BUILD_TYPE),release)
COMPILE_ASSERTS = 0
CFLAGS += -O2 -D_FORTIFY_SOURCE=2
CFLAGS += -DFIBER_BUILD_TYPE_RELEASE
else ifeq ($(BUILD_TYPE),debug)
CFLAGS += -O0 -g -fsanitize=undefined -fno-omit-frame-pointer
CFLAGS += -DFIBER_BUILD_TYPE_DEBUG
else ifeq ($(BUILD_TYPE),reldebug)
CFLAGS += -O2 -g -fsanitize=undefined -fno-omit-frame-pointer -D_FORTIFY_SOURCE=2
CFLAGS += -DFIBER_BUILD_TYPE_RELDEBUG
else ifeq ($(BUILD_TYPE),asan)
CFLAGS += -fsanitize=address -fsanitize=leak
CFLAGS += -O0 -g -fsanitize=undefined -fno-omit-frame-pointer
CFLAGS += -DFIBER_BUILD_TYPE_ASAN
else
$(error Invalid BUILD_TYPE '$(BUILD_TYPE)'. Expected one of: release, debug, reldebug, asan)
endif

ifeq ($(CC),clang)
include config/clang.mk
else ifeq ($(CC),gcc)
include config/gcc.mk
else ifeq ($(CC),cc)
CC_VERSION_OUT := $(shell $(CC) --version 2>/dev/null | tr A-Z a-z)

ifneq ($(findstring clang,$(CC_VERSION_OUT)),)
include config/clang.mk
else ifneq ($(findstring gcc,$(CC_VERSION_OUT)),)
include config/gcc.mk
else
$(error Invalid CC '$(CC)'. Expected clang or gcc.)
endif

else
$(error Invalid CC '$(CC)'. Expected clang or gcc.)
endif

CFLAGS += -DFIBER_VERSION_MAJOR=$(FIBER_VERSION_MAJOR)
CFLAGS += -DFIBER_VERSION_MINOR=$(FIBER_VERSION_MINOR)
CFLAGS += -DFIBER_VERSION_PATCH=$(FIBER_VERSION_PATCH)
CFLAGS += -DFIBER_BUILD_OPT_COMPILE_ASSERTS=$(COMPILE_ASSERTS)

SRCS := fbr_api.c fbr_futex.c fbr_jq_ring.c fbr_thread.c fbr_worker.c
OBJS := $(patsubst %.c,%.o,$(SRCS))

SRCS := $(patsubst %,$(SRC_DIR)/%,$(SRCS))
OBJS := $(patsubst %,$(OBJ_DIR)/%,$(OBJS))

VERBOSE ?= 0
ifeq ($(VERBOSE),1)
quiet_CC =
quiet_LD =
quiet_AR =
Q =
else
quiet_CC = echo " CC    $(subst $(FIBER_PATH)/,,$@)"
quiet_LD = echo " LD    $(subst $(FIBER_PATH)/,,$@)"
quiet_AR = echo " AR    $(subst $(FIBER_PATH)/,,$@)"
Q = @
endif

all: $(ALL_LIBS)

example: $(BUILD_DIR)/example

$(SHARED_LIB): LDFLAGS += -Wl,-soname,libfiber.so.$(FIBER_VERSION_MAJOR) -shared 
$(SHARED_LIB): $(OBJS)
	@$(quiet_LD)
	$(Q)$(LD) $(LDFLAGS) -o $(SHARED_LIB) $(OBJS) $(LDLIBS)

$(STATIC_LIB): $(OBJS)
	@$(quiet_AR)
	$(Q)$(AR) rcs $(STATIC_LIB) $(OBJS)

$(BUILD_DIR)/example: LDFLAGS += -Wl,-rpath,$(BUILD_DIR) -Wl,-rpath,/usr/local/lib
$(BUILD_DIR)/example: $(OBJ_DIR)/example.o $(SHARED_LIB)
	@$(quiet_LD)
	$(Q)$(LD) $(LDFLAGS) -o $@ $< -L$(BUILD_DIR) -lfiber $(LDLIBS)
	$(Q)ln -s  $(SHARED_LIB) $(SHARED_LIB).$(FIBER_VERSION_MAJOR)

$(OBJ_DIR)/example.o: $(FIBER_PATH)/example.c | $(OBJ_DIR)
	@$(quiet_CC)
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@$(quiet_CC)
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

-include $(OBJS:.o=.d)

$(OBJ_DIR):
	$(Q)mkdir -p $@

clean:
	$(Q)rm -rf $(BUILD_DIR)

.PHONY: all example clean
