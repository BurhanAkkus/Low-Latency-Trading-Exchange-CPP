#pragma once
#include <cstdint>
#include <limits>
#include "./macros.h"

namespace Common{
    using OrderId = uint64_t;
    constexpr auto OrderId_INVALID = std::numeric_limits<OrderId>::max(); // Prevents overflow.
    //ToString
    inline std::string orderIdToString(OrderId orderId){
        if(UNLIKELY(orderId == OrderId_INVALID)){
            return "INVALID";
        }
        return std::to_string(orderId);
    }
    inline bool isValid(OrderId orderId) noexcept {
        return orderId == OrderId_INVALID;
    }
}