#pragma once
namespace Common{
    template<typename T, typename U>
    inline void compareAndAssignMax(T& a, const U b) noexcept { if (b > a) a = static_cast<T>(b); }

    template<typename T, typename U>
    inline void compareAndAssignMin(T& a, const U b) noexcept { if (b < a) a = static_cast<T>(b); }}
}