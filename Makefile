-include .env.mk

# Build time options
COMPILE_ASSERTS ?= 1

FIBER_VERSION_MAJOR := 0
FIBER_VERSION_MINOR := 3
FIBER_VERSION_PATCH := 1
FIBER_VERSION := $(FIBER_VERSION_MAJOR).$(FIBER_VERSION_MINOR).$(FIBER_VERSION_PATCH)

CC ?= cc
LD := $(CC)
PKG_CONFIG ?= pkg-config

DESTDIR ?=
PREFIX ?= $(DESTDIR)/usr/local
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
PKGCONFIGDIR ?= $(LIBDIR)/pkgconfig

FIBER_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
SRC_DIR := $(FIBER_DIR)/src
BUILD_DIR := $(FIBER_DIR)/build
OBJ_DIR := $(BUILD_DIR)/objs

STATIC_LIB := $(BUILD_DIR)/libfiber.a
SHARED_LIB := $(BUILD_DIR)/libfiber.so
ALL_LIBS := $(STATIC_LIB) $(SHARED_LIB)

LDNAME = libfiber.so
LDNAME_MAJOR = libfiber.so.$(FIBER_VERSION_MAJOR)
LDNAME_VERSION = libfiber.so.$(FIBER_VERSION)

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
quiet_CC = echo " CC    $(subst $(FIBER_DIR)/,,$@)"
quiet_LD = echo " LD    $(subst $(FIBER_DIR)/,,$@)"
quiet_AR = echo " AR    $(subst $(FIBER_DIR)/,,$@)"
Q = @
endif

all: $(ALL_LIBS)

example: $(BUILD_DIR)/example

$(SHARED_LIB): LDFLAGS += -Wl,-soname,$(LDNAME_MAJOR) -shared
$(SHARED_LIB): $(OBJS)
	@$(quiet_LD)
	$(Q)$(LD) $(LDFLAGS) -o $(SHARED_LIB) $(OBJS) $(CK_LDFLAGS)

$(STATIC_LIB): $(OBJS)
	@$(quiet_AR)
	$(Q)$(AR) rcs $(STATIC_LIB) $(OBJS)

$(BUILD_DIR)/example: LDFLAGS += -Wl,-rpath,$(BUILD_DIR)
$(BUILD_DIR)/example: $(OBJ_DIR)/example.o $(SHARED_LIB)
	@$(quiet_LD)
	$(Q)$(LD) $(LDFLAGS) -o $@ $< -L$(BUILD_DIR) -lfiber
	$(Q)ln -sf  $(SHARED_LIB) $(SHARED_LIB).$(FIBER_VERSION_MAJOR)

$(OBJ_DIR)/example.o: $(FIBER_DIR)/example.c | $(OBJ_DIR)
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
	$(Q)rm -f fiber.pc

docs:
	$(Q)doxygen Doxyfile

docs-open:
	$(Q)$$BROWSER docs/html/index.html

install-headers:
	$(Q)mkdir -p $(INCLUDEDIR)
	$(Q)cp -p $(FIBER_DIR)/include/*.h $(INCLUDEDIR)
	$(Q)chmod 644 $(INCLUDEDIR)/fbr*.h

install-so:
	$(Q)mkdir -p $(LIBDIR)
	$(Q)cp -p $(BUILD_DIR)/$(LDNAME) $(LIBDIR)/$(LDNAME_VERSION)
	$(Q)ln -sf $(LDNAME_VERSION) $(LIBDIR)/$(LDNAME)
	$(Q)ln -sf $(LDNAME_VERSION) $(LIBDIR)/$(LDNAME_MAJOR)
	$(Q)chmod 755 \
		$(LIBDIR)/$(LDNAME_VERSION) \
		$(LIBDIR)/$(LDNAME) \
		$(LIBDIR)/$(LDNAME_MAJOR)

install-static:
	$(Q)mkdir -p $(LIBDIR)
	$(Q)cp -p $(BUILD_DIR)/libfiber.a $(LIBDIR)/libfiber.a
	$(Q)chmod 644 $(LIBDIR)/libfiber.a

install-pc: fiber.pc
	$(Q)mkdir -p $(PKGCONFIGDIR)
	$(Q)cp -p fiber.pc $(PKGCONFIGDIR)/fiber.pc

install: all install-headers install-so install-static install-pc
	@echo 'Successfully installed to $(PREFIX)'

uninstall:
	$(Q)rm -f $(INCLUDEDIR)/fbr*.h
	$(Q)rm -f $(LIBDIR)/$(LDNAME) $(LIBDIR)/$(LDNAME_MAJOR) $(LIBDIR)/$(LDNAME_VERSION)
	$(Q)rm -f $(LIBDIR)/libfiber.a
	$(Q)rm -f $(PKGCONFIGDIR)/fiber.pc

fiber.pc:
	@echo 'prefix=$(PREFIX)' > $@
	@echo 'exec_prefix=$${prefix}' >> $@
	@echo 'includedir=$${prefix}/include' >> $@
	@echo 'libdir=$${exec_prefix}/lib' >> $@
	@echo '' >> $@
	@echo 'Name: Fiber' >> $@
	@echo 'Description: Lock-free thread pool library built on top of POSIX threads.' >> $@
	@echo 'URL: https://github.com/carterww/fiber' >> $@
	@echo 'Version: $(FIBER_VERSION)' >> $@
	@echo 'Libs: -L$${libdir} -lfiber' >> $@
	@echo 'Libs.private: -lck' >> $@
	@echo 'Cflags: -I$${includedir}' >> $@

.PHONY: all example clean \
	docs \
	install-headers install-so install-static install-pc install uninstall
