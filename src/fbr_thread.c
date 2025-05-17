#include "fbr_platform.h"

#if defined(FBR_OS_LINUX) || defined(FBR_OS_FREEBSD)
#include "posix/fbr_posix_thread.c"
#else
#error "No thread implementation provided"
#endif /* FBR_OS */
