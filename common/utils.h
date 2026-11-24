#pragma once
#include <malloc.h>
#include "macros.h"
namespace Common{
    template<typename T, typename U>
    inline void compareAndAssignMax(T& a, const U b) noexcept { if (b > a) a = static_cast<T>(b); }

    template<typename T, typename U>
    inline void compareAndAssignMin(T& a, const U b) noexcept { if (b < a) a = static_cast<T>(b); }

    // malloc keeps freed memory for later allocations instead of returning it to the kernel.
    // MatchingEngine's warm-up needs it. Call it first in main(), mallopt isn't thread safe.
    inline void keepFreedMemory() noexcept { ASSERT(mallopt(M_TRIM_THRESHOLD, -1) == 1, "mallopt failed."); }
}