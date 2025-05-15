cmake_minimum_required(VERSION 3.18)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)
project(fiber VERSION 0.2.3)

# Pthreads required
find_package(Threads REQUIRED)

find_library(CK_LIBRARY ck PATHS /usr/local/lib /usr/lib REQUIRED)
get_filename_component(CK_LIB_DIR ${CK_LIBRARY} DIRECTORY)
get_filename_component(CK_ROOT ${CK_LIB_DIR} DIRECTORY)
set(CK_INCLUDE_DIR "${CK_ROOT}/include")

set(LIB_TARGET ${PROJECT_NAME})
set(THREAD_TARGET fiber_thread)
set(FUTEX_TARGET fiber_futex)
set(JQ_RING_TARGET fiber_jq_ring)

set(COMMON_COMPILE_DEFS "")
set(COMMON_INTERNAL_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CK_INCLUDE_DIR}
)

function(create_build_bool_option opt_name desc default)
    option(${opt_name} ${desc} ${default})
    if(${opt_name})
        set(${opt_name}_ENABLED 1)
    else()
        set(${opt_name}_ENABLED 0)
    endif()
    set(def FIBER_BUILD_OPT_${opt_name}=${${opt_name}_ENABLED})
    set(COMMON_COMPILE_DEFS "${COMMON_COMPILE_DEFS};${def}" PARENT_SCOPE)
endfunction()

function(create_build_enum_option opt_name desc default)
    set(${opt_name} ${default} CACHE STRING ${desc})
    if(NOT ${${opt_name}} IN_LIST ARGN)
        message(FATAL_ERROR "${opt_name} is invalid: ${${opt_name}}. Select one of ${ARGN}")
    endif()
    string(TOUPPER ${${opt_name}} ${opt_name}_UPPER)
    set(def FIBER_BUILD_OPT_${opt_name}_${${opt_name}_UPPER})
    set(COMMON_COMPILE_DEFS "${COMMON_COMPILE_DEFS};${def}" PARENT_SCOPE)
endfunction()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build." FORCE)
endif()

set(WARNING_FLAGS
	-Werror -Wall -Wextra -Wpedantic -Wno-unused -Wfloat-equal
	-Wdouble-promotion -Wformat=2 -Wformat-security -Wstack-protector
	-Walloca -Wvla -Wcast-qual -Wconversion -Wformat-signedness -Wshadow
	-Wstrict-overflow=4 -Wundef -Wstrict-prototypes -Wswitch-default
	-Wswitch-enum -Wnull-dereference -Wmissing-include-dirs
)
set(DEBUG_FLAGS
	"-g -fsanitize=undefined -fno-omit-frame-pointer"
)
set(SECURITY_FLAGS
	-fstack-protector-strong -fstack-clash-protection
	-fsanitize=bounds -fsanitize-undefined-trap-on-error
	-fvisibility=hidden
)
set(LINKER_SECURITY_FLAGS
	-Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -Wl,-z,separate-code
)
set(ADDRESS_SANITIZER_FLAGS
	-fsanitize=address -fsanitize=leak
)

# GCC only flags
set (GCC_WARNING_FLAGS
	-Wformat-overflow=2 -Wformat-truncation=2 -Wtrampolines
	-Warray-bounds=2 -Wimplicit-fallthrough=3 -Wlogical-op
	-Wshift-overflow=2 -Wstringop-overflow=4 -Warith-conversion
	-Wduplicated-cond -Wduplicated-branches -Wstack-usage=10000
	-Wcast-align=strict
)
set (GCC_DEBUG_FLAGS
	-fsanitize=bounds-strict -fanalyzer
)
set (GCC_SECURITY_FLAGS "")
set(GCC_ADDRESS_SANITIZER_FLAGS
	-fsanitize=pointer-compare -fsanitize=pointer-subtract
)

# Clang only flags
set (CLANG_WARNING_FLAGS
	-Warray-bounds -Warray-bounds-pointer-arithmetic -Wassign-enum
	-Wbad-function-cast -Wconditional-uninitialized -Wformat-type-confusion
	-Widiomatic-parentheses -Wimplicit-fallthrough -Wloop-analysis
	-Wpointer-arith -Wshift-sign-overflow -Wshorten-64-to-32
	-Wtautological-constant-in-range-compare -Wunreachable-code-aggressive
	-Wthread-safety -Wthread-safety-beta -Wcomma
)
set (CLANG_DEBUG_FLAGS
	-fsanitize=integer
)
set (CLANG_SECURITY_FLAGS
	-fsanitize=safe-stack
)
set(CLANG_ADDRESS_SANITIZER_FLAGS "")

# Add compiler specific flags to flags
if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
	set(WARNING_FLAGS ${WARNING_FLAGS} ${GCC_WARNING_FLAGS})
	set(DEBUG_FLAGS ${DEBUG_FLAGS} ${GCC_DEBUG_FLAGS})
	set(SECURITY_FLAGS ${SECURITY_FLAGS} ${GCC_SECURITY_FLAGS})
	set(ADDRESS_SANITIZER_FLAGS ${ADDRESS_SANITIZER_FLAGS} ${GCC_ADDRESS_SANITIZER_FLAGS})
elseif (CMAKE_C_COMPILER_ID STREQUAL "Clang")
	set(WARNING_FLAGS ${WARNING_FLAGS} ${CLANG_WARNING_FLAGS})
	set(DEBUG_FLAGS ${DEBUG_FLAGS} ${CLANG_DEBUG_FLAGS})
	set(SECURITY_FLAGS ${SECURITY_FLAGS} ${CLANG_SECURITY_FLAGS})
	set(ADDRESS_SANITIZER_FLAGS ${ADDRESS_SANITIZER_FLAGS} ${CLANG_ADDRESS_SANITIZER_FLAGS})
else()
	message(FATAL_ERROR "${CMAKE_C_COMPILER_ID} is not a valid compiler. Fiber required Clang or GCC")
endif()

# Add sanitizer flags from sanitizer option
if (SANITIZER STREQUAL "thread")
	set(DEBUG_FLAGS ${DEBUG_FLAGS} -fsanitize=thread)
elseif (SANITIZER STREQUAL "address")
	set(DEBUG_FLAGS ${DEBUG_FLAGS} ${ADDRESS_SANITIZER_FLAGS})
endif()

# Other sanitizers don't like safe-stack
if("-fsanitize=integer" IN_LIST DEBUG_FLAGS OR
		"-fsanitize=address" IN_LIST DEBUG_FLAGS OR
		"-fsanitize=thread" IN_LIST DEBUG_FLAGS)
	list(REMOVE_ITEM SECURITY_FLAGS "-fsanitize=safe-stack")
endif()

# Disable PIC if using thread sanitizer and not shared lib
if (NOT BUILD_SHARED_LIBS AND "-fsanitize=thread" IN_LIST DEBUG_FLAGS)
	set(PIC OFF FORCE)
endif()

set(CMAKE_BUILD_TYPE "Test" CACHE STRING "")

list(JOIN DEBUG_FLAGS " " DEBUG_FLAGS_STRING)

set(CMAKE_C_FLAGS_DEBUG "-O0 ${DEBUG_FLAGS_STRING} -DFIBER_BUILD_OPT_ENV_DEBUG" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_RELEASE "-O2 -DFIBER_BUILD_OPT_ENV_REL -D_FORTIFY_SOURCE=2" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -DFIBER_BUILD_OPT_ENV_DEBUG -D_FORTIFY_SOURCE=2" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_MINSIZEREL "-Os -DFIBER_BUILD_OPT_ENV_REL -D_FORTIFY_SOURCE=2" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_TEST "-O2 ${DEBUG_FLAGS_STRING} -DFIBER_BUILD_OPT_ENV_TEST -D_FORTIFY_SOURCE=2" CACHE STRING "" FORCE)

# Add flags
# Warning/Error flags
add_compile_options(${WARNING_FLAGS})
# Security flags
add_compile_options(${SECURITY_FLAGS})
# Linker flags
add_link_options(${LINKER_SECURITY_FLAGS})

# Set properties
set_property(GLOBAL PROPERTY POSITION_INDEPENDENT_CODE ${PIC})

# Boolean options
create_build_bool_option(COMPILE_ASSERTS
    "Compile fiber_assert and panic statements into the library"
    ON
)
create_build_bool_option(COMPILE_FIFO_QUEUE
    "Compile fiber_fifo into the library"
    ON
)
create_build_bool_option(PIC
    "Enable PIC for static library builds"
    ON
)

# Enum options
create_build_enum_option(SANITIZER
    "Which sanitizer to use in debug and test builds"
    none
    none address thread
)
create_build_enum_option(THREAD_IMPL
    "Which threading implementation to use"
    posix
    posix
)
create_build_enum_option(MUTEX_IMPL
    "Which mutex implementation to use"
    posix
    posix
)
create_build_enum_option(SEMAPHORE_IMPL
    "Which semaphore implementation to use"
    posix
    posix
)
create_build_enum_option(FUTEX_IMPL
    "Which futex implementation to use"
    linux
    linux
)
set(LIB_SOURCE_FILES
    src/fbr_api.c
    src/fbr_worker.c
)

if (THREAD_IMPL STREQUAL "posix")
	set(THREAD_SOURCE_FILES src/posix/fbr_posix_thread.c)
endif()
if (FUTEX_IMPL STREQUAL "linux")
	set(FUTEX_SOURCE_FILES src/linux/fbr_linux_futex.c)
endif()
add_library(${LIB_TARGET}_static STATIC ${LIB_SOURCE_FILES})
add_library(${LIB_TARGET}_shared SHARED ${LIB_SOURCE_FILES})
set_target_properties(${LIB_TARGET}_static PROPERTIES OUTPUT_NAME ${LIB_TARGET})
set_target_properties(${LIB_TARGET}_shared PROPERTIES OUTPUT_NAME ${LIB_TARGET})
add_library(${THREAD_TARGET} STATIC ${THREAD_SOURCE_FILES})
add_library(${FUTEX_TARGET} STATIC ${FUTEX_SOURCE_FILES})
add_library(${JQ_RING_TARGET} STATIC src/fbr_jq_ring.c)

target_link_libraries(${LIB_TARGET}_static ${CK_LIBRARY})
target_link_libraries(${LIB_TARGET}_static ${THREAD_TARGET})
target_link_libraries(${LIB_TARGET}_static ${FUTEX_TARGET})
target_link_libraries(${LIB_TARGET}_static ${JQ_RING_TARGET})

target_link_libraries(${LIB_TARGET}_shared ${CK_LIBRARY})
target_link_libraries(${LIB_TARGET}_shared ${THREAD_TARGET})
target_link_libraries(${LIB_TARGET}_shared ${FUTEX_TARGET})
target_link_libraries(${LIB_TARGET}_shared ${JQ_RING_TARGET})

target_include_directories(${LIB_TARGET}_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(${LIB_TARGET}_shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(${LIB_TARGET}_static PUBLIC
    FIBER_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    FIBER_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    FIBER_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)
target_compile_definitions(${LIB_TARGET}_static PRIVATE
    ${COMMON_COMPILE_DEFS}
)
target_compile_definitions(${LIB_TARGET}_shared PUBLIC
    FIBER_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    FIBER_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    FIBER_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)
target_compile_definitions(${LIB_TARGET}_shared PRIVATE
    ${COMMON_COMPILE_DEFS}
)

target_compile_definitions(${THREAD_TARGET} PRIVATE
    ${COMMON_COMPILE_DEFS}
)
target_compile_definitions(${FUTEX_TARGET} PRIVATE
    ${COMMON_COMPILE_DEFS}
)
target_compile_definitions(${JQ_RING_TARGET} PRIVATE
    ${COMMON_COMPILE_DEFS}
)

target_include_directories(${LIB_TARGET}_static PRIVATE
    ${COMMON_INTERNAL_INCLUDES}
)
target_include_directories(${LIB_TARGET}_shared PRIVATE
    ${COMMON_INTERNAL_INCLUDES}
)
target_include_directories(${THREAD_TARGET} PRIVATE
    ${COMMON_INTERNAL_INCLUDES}
)
target_include_directories(${FUTEX_TARGET} PRIVATE
    ${COMMON_INTERNAL_INCLUDES}
)
target_include_directories(${JQ_RING_TARGET} PRIVATE
    ${COMMON_INTERNAL_INCLUDES}
)

add_executable(example example.c)
target_link_libraries(example ${LIB_TARGET}_shared)
