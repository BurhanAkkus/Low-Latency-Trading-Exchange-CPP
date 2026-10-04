#pragma once
#include <cstdint>
#include <limits>
#include "./macros.h"
#include <string>
namespace Common{
    /*
        ToString functions seem like they allocate dynamically but they don't.
        Upto 15 characters (10^15) is stored in the std::string object with libstdc++,
        this is upto 22 characters with libc++.

        They are also noexcept even though they can throw std::bad_alloc.
        We won't do exception handling so when they throw the program exits anyway.
    */

    // === OrderId ===
    using OrderId = uint64_t;
    constexpr auto OrderId_INVALID = std::numeric_limits<OrderId>::max(); // Uninitialized.

    constexpr inline bool isValidOrderId(OrderId orderId) noexcept {
        return orderId != OrderId_INVALID;
    }
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
    inline auto tickerIdToString(TickerId tickerId) -> std::string{
        if (UNLIKELY(!isValidTickerId(tickerId)))
        { return "INVALID";}
        return std::to_string(tickerId);
    }

    // === ClientId ===
    using ClientId = uint64_t;
    constexpr auto ClientId_INVALID = std::numeric_limits<ClientId>::max();
    constexpr bool isValidClientId(ClientId clientId) noexcept{
        return clientId != ClientId_INVALID;
    }
    inline auto clientIdToString(ClientId clientId) noexcept -> std::string{
        if(UNLIKELY(!isValidClientId(clientId))){
            return "INVALID";
        }
        return std::to_string(clientId);
    }
}