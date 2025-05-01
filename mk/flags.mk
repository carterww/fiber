# The warning, security, and debug flags were mostly taken from this article:
# https://airbus-seclab.github.io/c-compiler-security/

# These are the default flags that should work for clang and gcc. More specific
# flags my be added based on CC.

C_PIC_FLAG = -fPIC
C_STD_FLAG = -std=c89

C_OPTIMIZE_FLAGS = -O2

C_WARNING_FLAGS = -Werror -Wall -Wextra -Wpedantic -Wno-unused -Wfloat-equal \
		  -Wdouble-promotion -Wformat=2 -Wformat-security -Wstack-protector \
		  -Walloca -Wvla -Wcast-qual -Wconversion -Wformat-signedness -Wshadow \
		  -Wstrict-overflow=4 -Wundef -Wstrict-prototypes -Wswitch-default \
		  -Wswitch-enum -Wnull-dereference -Wmissing-include-dirs

C_SECURITY_FLAGS = -fstack-protector-strong -fstack-clash-protection \
		   -D_FORTIFY_SOURCE=2 -fsanitize=bounds -fsanitize-undefined-trap-on-error

C_DEBUG_FLAGS = -fsanitize=undefined -fno-omit-frame-pointer
		   
C_INCLUDE_FLAGS = -I. -I./src/internal -I./include -I./deps/fiber_atomic/include -I./deps/fiber_lock/include

LD_SECURITY_FLAGS = -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -Wl,-z,separate-code

COMPILER_VERSION_OUTPUT = $(shell $(CC) --version | tr A-Z a-z)

# Add compiler specific flags
ifeq ($(findstring gcc,$(COMPILER_VERSION_OUTPUT)),gcc)
C_WARNING_FLAGS += -Wformat-overflow=2 -Wformat-truncation=2 -Wtrampolines \
		   -Warray-bounds=2 -Wimplicit-fallthrough=3 -Wlogical-op \
		   -Wshift-overflow=2 -Wstringop-overflow=4 -Warith-conversion \
		   -Wduplicated-cond -Wduplicated-branches -Wstack-usage=10000 \
		   -Wcast-align=strict
C_DEBUG_FLAGS += -fsanitize=bounds-strict -fanalyzer
# Add sanitize flags
ifeq ($(DEBUG_SANITIZE),thread)
C_DEBUG_FLAGS += -fsanitize=thread
ifneq ($(ENV),norm)
	# Thread sanitizer doesn't like PIC
	PIC = 0
endif
else ifeq ($(DEBUG_SANITIZE),address)
C_DEBUG_FLAGS += -fsanitize=address -fsanitize=leak -fsanitize=pointer-compare \
		 -fsanitize=pointer-subtract
endif
endif

ifeq ($(findstring clang,$(COMPILER_VERSION_OUTPUT)),clang)
C_WARNING_FLAGS += -Warray-bounds -Warray-bounds-pointer-arithmetic -Wassign-enum \
		   -Wbad-function-cast -Wconditional-uninitialized -Wformat-type-confusion \
		   -Widiomatic-parentheses -Wimplicit-fallthrough -Wloop-analysis \
		   -Wpointer-arith -Wshift-sign-overflow -Wshorten-64-to-32 \
		   -Wtautological-constant-in-range-compare -Wunreachable-code-aggressive \
		   -Wthread-safety -Wthread-safety-beta -Wcomma
C_DEBUG_FLAGS += -fsanitize=integer
# -fsanitize=safe-stack is incompatible with many of the other sanitizers (at least thread,
#  integer, and address). Don't add for debug or test mode.
ifeq ($(ENV),norm)
C_SECURITY_FLAGS += -fsanitize=safe-stack
endif
# Add sanitize flags
ifeq ($(DEBUG_SANITIZE),thread)
C_DEBUG_FLAGS += -fsanitize=thread
ifneq ($(ENV),norm)
	# Thread sanitizer doesn't like PIC
	PIC = 0
endif
else ifeq ($(DEBUG_SANITIZE),address)
C_DEBUG_FLAGS += -fsanitize=address -fsanitize=leak
endif
endif

# C_CONFIG_FLAGS is set by common.mk
C_FLAGS = $(C_STD_FLAG) $(C_OPTIMIZE_FLAGS) $(C_WARNING_FLAGS) \
	  $(C_SECURITY_FLAGS) $(C_INCLUDE_FLAGS) $(C_CONFIG_FLAGS)

LD_FLAGS = $(LD_SECURITY_FLAGS)

ifeq ($(ENV),$(filter $(ENV),debug test))
C_FLAGS += $(C_DEBUG_FLAGS)
endif

ifeq ($(PIC),1)
C_FLAGS += $(C_PIC_FLAG)
endif
