#include "fbr_platform.h"

#if defined(FBR_OS_LINUX)
#include "linux/fbr_linux_futex.c"
#else
#error "No futex implementation provided"
#endif /* FBR_OS */
