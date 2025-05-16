/* See LICENSE file for copyright and license details. */

#ifndef _FBR_CC_H
#define _FBR_CC_H

/* clang also defined __GNUC__ because it's "compatible" but I want to
 * know if it is really gcc.
 */
#if defined(__GNUC__) && !defined(__clang__)
#define FBR_CC_GCC
#elif defined(__clang__)
#define FBR_CC_CLANG
#else
#error Detected an unsupported compiler.
#endif

#if defined(FBR_CC_GCC) || defined(FBR_CC_CLANG)

#define fbr_alignof(t) __alignof__(t)
#define fbr_typeof(e) __typeof__(e)
#define fbr_expect(expr, expect) __builtin_expect(expr, expect)

#define fbr_compiler_barrier() __asm__ __volatile__("" ::: "memory");
#define fbr_unreachable() __builtin_unreachable()
#define fbr_assume_aligned(p, a) __builtin_assume_aligned(p, a)
#define fbr_prefetch(p) __builtin_prefetch(p)

#define FBR_ATTR_ALIGNED(a) __attribute__((aligned(a)))
#define FBR_ATTR_ALLOC_ALIGNED(arg_p) __attribute__((alloc_align(arg_p)))
#define FBR_ATTR_NORETURN __attribute__((noreturn))
#define FBR_ATTR_PACKED __attribute__((packed))
#define FBR_ATTR_ALWAYS_INLINE __attribute__((always_inline))
#define FBR_ATTR_COLD __attribute__((cold))
#define FBR_ATTR_HOT __attribute__((hot))
#define FBR_ATTR_CONST __attribute__((const))
#define FBR_ATTR_WEAK __attribute__((weak))
#define FBR_ATTR_PUBLIC __attribute__((visibility("default")))
#define FBR_ATTR_PRIVATE __attribute__((visibility("hidden")))

#if defined(FBR_CC_CLANG)
#define FBR_ATTR_NO_SANITIZE_OVERFLOW \
	__attribute__((no_sanitize("unsigned-integer-overflow")))
#else
#define FBR_ATTR_NO_SANITIZE_OVERFLOW
#endif

#endif /* gcc or clang */

#endif /* _FBR_CC_H */
