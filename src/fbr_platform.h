// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Carter Williams

#ifndef _FBR_PLATFORM_H
#define _FBR_PLATFORM_H

#define FBR_CACHELINE_BYTES ((size_t)64)
#define FBR_ALIGNMENT_MIN ((size_t)8)

#define FBR_SIZE_ROUND_ALIGNMENT(size, a) \
	((size_t)(size + a - 1) & ~(size_t)(a - 1))
#define FBR_SIZE_ROUND_CACHELINE(size) \
	FBR_SIZE_ROUND_ALIGNMENT(size, FBR_CACHELINE_BYTES)
#define FBR_SIZE_ROUND_MIN_ALIGNMENT(size) \
	FBR_SIZE_ROUND_ALIGNMENT(size, FBR_ALIGNMENT_MIN)

/* Figure out the architecture */
#if defined(__x86_64__) || defined(__am64__)
#define FBR_ARCH_X86_64
#elif defined(__arm__)
#define FBR_ARCH_ARM
#if defined(__thumb__)
#define FBR_ARCH_ARM_THUMB
#else
#define FBR_ARCH_ARM_NO_THUMB
#endif /* __thumb__ */
#elif defined(__aarch64__)
#define FBR_ARCH_ARM64
#elif defined(__i386__)
#define FBR_ARCH_X86
#elif defined(__risv) && (__risv_xlen == 64)
#define FBR_ARCH_RISCV_64
#else
#error Detected an unsupported architecture.
#endif /* arch */

#if defined(FBR_ARCH_X86_64) || defined(FBR_ARCH_ARM64) || \
	defined(FBR_ARCH_RISCV64)
#define FBR_ARCH_64_BIT
#elif defined(FBR_ARCH_ARM) || defined(FBR_ARCH_X86)
#define FBR_ARCH_32_BIT
#endif

/* Figure out the OS */
#if defined(__linux__)
#define FBR_OS_LINUX
#elif defined(__FreeBSD__)
#define FBR_OS_FREEBSD
#else
#error Detected an unsupported operating system.
#endif /* os */

#endif /* _FBR_PLATFORM_H */
