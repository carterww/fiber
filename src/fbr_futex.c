// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Carter Williams

#include "fbr_platform.h"

#if defined(FBR_OS_LINUX)
#include "linux/fbr_linux_futex.c"
#else
#error "No futex implementation provided"
#endif /* FBR_OS */
