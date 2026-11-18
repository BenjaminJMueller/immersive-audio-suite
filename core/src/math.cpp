#include "rk/math.hpp"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#define RK_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define RK_ARM64 1
#endif

namespace rk {

ScopedNoDenormals::ScopedNoDenormals() noexcept {
#if defined(RK_X86)
    previous_ = static_cast<std::intptr_t>(_mm_getcsr());
    _mm_setcsr(static_cast<unsigned int>(previous_) | 0x8040u); // FTZ | DAZ
#elif defined(RK_ARM64) && !defined(_MSC_VER)
    std::intptr_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    previous_ = fpcr;
    const std::intptr_t next = fpcr | (1 << 24); // FZ
    asm volatile("msr fpcr, %0" : : "r"(next));
#endif
}

ScopedNoDenormals::~ScopedNoDenormals() noexcept {
#if defined(RK_X86)
    _mm_setcsr(static_cast<unsigned int>(previous_));
#elif defined(RK_ARM64) && !defined(_MSC_VER)
    asm volatile("msr fpcr, %0" : : "r"(previous_));
#endif
}

} // namespace rk
