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

    // === Price ===
    using Price = uint64_t;
    constexpr auto Price_INVALID = std::numeric_limits<Price>::max();
    constexpr bool isValidPrice(Price price) noexcept{
        return price != Price_INVALID;
    }
    inline auto priceToString(Price price) noexcept -> std::string{
        if(UNLIKELY(!isValidPrice(price))){
            return "INVALID";
        }
        return std::to_string(price);
    }

    // === Quantity ===
    using Qty = uint64_t;
    constexpr auto Qty_INVALID = std::numeric_limits<Qty>::max();
    constexpr bool isValidQty(Qty qty) noexcept{
        return qty != Price_INVALID;
    }
    inline auto qtyToString(Qty qty) noexcept -> std::string{
        if(UNLIKELY(!isValidQty(qty))){
            return "INVALID";
        }
        return std::to_string(qty);
    }

    // === Priority ===
    using Priority = uint64_t;
    constexpr auto Priority_INVALID = std::numeric_limits<Priority>::max();
    constexpr bool isValidPriority(Priority priority) noexcept{
        return priority != Priority_INVALID;
    }
    inline auto priorityToString(Priority priority) noexcept -> std::string{
        if(UNLIKELY(!isValidPriority(priority))){
            return "INVALID";
        }
        return std::to_string(priority);
    }

    // === Side ===
    enum class Side:uint8_t{
        INVALID = 0,
        SELL = 1,
        BUY = 2
    };

    inline auto sideToString(Side side)noexcept -> std::string{
        switch(side){
            case Side::INVALID:
                return "INVALID";
            case Side::SELL:
                return "SELL";
            case Side::BUY:
                return "BUY";
        }
        return "UNKNOWN"; // here incase a new side gets added.
    }
}