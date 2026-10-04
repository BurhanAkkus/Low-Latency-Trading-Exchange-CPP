#pragma once
#include <cstdint>
#include <limits>
#include "./macros.h"
#include <string>
namespace Common{
    // === OrderId ===
    using OrderId = uint64_t;
    constexpr auto OrderId_INVALID = std::numeric_limits<OrderId>::max(); // Uninitialized.

    constexpr inline bool isValidOrderId(OrderId orderId) noexcept {
        return orderId != OrderId_INVALID;
    }
    //ToString
    // Check
    // Allocates on the Heap. Keep of the Hot Path!
    inline std::string orderIdToString(OrderId orderId){
        if(UNLIKELY(!isValidOrderId(orderId))){
            return "INVALID";
        }
        return std::to_string(orderId);
    }


    // === TickerId ===
    using TickerId = uint64_t;
    constexpr auto TickerId_INVALID = std::numeric_limits<TickerId>::max();

    constexpr inline bool isValidTickerId(TickerId tickerId) noexcept{
        return tickerId != TickerId_INVALID;
    }
    // ToString
    // Check
    // Allocates on the Heap. Keep of the Hot Path!
    inline auto tickerIdToString(TickerId tickerId) -> std::string{
        if (UNLIKELY(!isValidTickerId(tickerId)))
        { return "INVALID";}
        return std::to_string(tickerId);
    }

    
}