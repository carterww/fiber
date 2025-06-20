// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Carter Williams

#include "fbr_platform.h"

#if defined(FBR_OS_LINUX) || defined(FBR_OS_FREEBSD)
#include "posix/fbr_posix_thread.c"
#else
#error "No thread implementation provided"
#endif /* FBR_OS */
