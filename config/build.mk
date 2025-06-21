FIBER_VERSION_MAJOR ?= 0
FIBER_VERSION_MINOR ?= 3
FIBER_VERSION_PATCH ?= 2

CC ?= cc
LD := $(CC)
PKG_CONFIG ?= pkg-config

DESTDIR ?=
PREFIX ?= $(DESTDIR)/usr/local
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
PKGCONFIGDIR ?= $(LIBDIR)/pkgconfig

FIBER_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
SRC_DIR := $(FIBER_DIR)/src
BUILD_DIR := $(FIBER_DIR)/build
OBJ_DIR := $(BUILD_DIR)/objs

CFLAGS += -std=c99 -fPIC -pthread -I$(FIBER_DIR)/include -I$(SRC_DIR)
CFLAGS += -MMD -MP
CFLAGS += $(shell $(PKG_CONFIG) --cflags ck 2>/dev/null)

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
LDFLAGS += -Wl,-rpath,$(shell $(PKG_CONFIG) --variable=libdir --shared ck 2>/dev/null)

CK_LDFLAGS += $(shell $(PKG_CONFIG) --libs --shared ck 2>/dev/null)

include $(FIBER_DIR)/config/config.mk

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
include $(FIBER_DIR)/config/clang.mk
else ifeq ($(CC),gcc)
include $(FIBER_DIR)/config/gcc.mk
else ifeq ($(CC),cc)
CC_VERSION_OUT := $(shell $(CC) --version 2>/dev/null | tr A-Z a-z)

ifneq ($(findstring clang,$(CC_VERSION_OUT)),)
include $(FIBER_DIR)/config/clang.mk
else ifneq ($(findstring gcc,$(CC_VERSION_OUT)),)
include $(FIBER_DIR)/config/gcc.mk
else
$(error Invalid CC '$(CC)'. Expected clang or gcc.)
endif

else
$(error Invalid CC '$(CC)'. Expected clang or gcc.)
endif

FIBER_VERSION := $(FIBER_VERSION_MAJOR).$(FIBER_VERSION_MINOR).$(FIBER_VERSION_PATCH)

CFLAGS += -DFIBER_VERSION_MAJOR=$(FIBER_VERSION_MAJOR)
CFLAGS += -DFIBER_VERSION_MINOR=$(FIBER_VERSION_MINOR)
CFLAGS += -DFIBER_VERSION_PATCH=$(FIBER_VERSION_PATCH)
CFLAGS += -DFIBER_BUILD_OPT_COMPILE_ASSERTS=$(COMPILE_ASSERTS)
