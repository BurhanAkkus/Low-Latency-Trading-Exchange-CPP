#pragma once
#include <cstdint>
#include <limits>
#include "./macros.h"

namespace Common{
    using OrderId = uint64_t;
    constexpr auto OrderId_INVALID = std::numeric_limits<OrderId>::max(); // Uninitialized.
    //ToString
    // Check
    // Allocates on the Heap. Keep of the Hot Path!
    inline std::string orderIdToString(OrderId orderId){
        if(UNLIKELY(orderId == OrderId_INVALID)){
            return "INVALID";
        }
        return std::to_string(orderId);
    }

    constexpr inline bool isValid(OrderId orderId) noexcept {
        return orderId != OrderId_INVALID;
    }
}