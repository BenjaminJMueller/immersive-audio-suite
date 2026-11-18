// Counts heap allocations made on the current thread while a guard is active.
// Used to prove that Engine::process never allocates (realtime safety).
#pragma once

#include <cstddef>

namespace rktest {

struct AllocGuard {
    AllocGuard();
    ~AllocGuard();
    std::size_t allocations() const;
};

} // namespace rktest
