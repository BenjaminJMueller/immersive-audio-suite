#include "alloc_guard.hpp"

#include <cstdlib>
#include <new>

namespace {
thread_local bool g_active = false;
thread_local std::size_t g_count = 0;

void* allocate(std::size_t n) {
    if (g_active) ++g_count;
    if (n == 0) n = 1;
    void* p = std::malloc(n);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}
} // namespace

namespace rktest {
AllocGuard::AllocGuard() { g_count = 0; g_active = true; }
AllocGuard::~AllocGuard() { g_active = false; }
std::size_t AllocGuard::allocations() const { return g_count; }
} // namespace rktest

void* operator new(std::size_t n) { return allocate(n); }
void* operator new[](std::size_t n) { return allocate(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    try { return allocate(n); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    try { return allocate(n); } catch (...) { return nullptr; }
}
void operator delete(void* p) noexcept { if (g_active && p) ++g_count; std::free(p); }
void operator delete[](void* p) noexcept { if (g_active && p) ++g_count; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { if (g_active && p) ++g_count; std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { if (g_active && p) ++g_count; std::free(p); }
