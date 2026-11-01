#pragma once
#include <cstdint>
#include <limits>
#include "./macros.h"

namespace Common{
    // x86-64. std::hardware_destructive_interference_size warns (-Winterference-size) in headers.
    constexpr size_t CACHE_LINE_SIZE = 64;
    // Logger
    constexpr size_t LOG_QUEUE_SIZE = 8 * 1024 * 1024;
    // Matching Engine
    constexpr size_t ME_MAX_TICKERS = 8;
    constexpr size_t ME_MAX_CLIENT_UPDATES = 256 * 1024;
    constexpr size_t ME_MAX_MARKET_UPDATES = 256 * 1024;
    constexpr size_t ME_MAX_NUM_CLIENTS = 256;
    // Market order ids per ticker, ids are not reused: every new order over the exchange's
    // lifetime counts. The snapshot synthesizer indexes its orders by them.
    constexpr size_t ME_MAX_ORDER_IDS = 1024 * 1024;
    // Orders resting in one order book at the same time. The engine benchmark (500k requests)
    // peaks at ~8k per ticker; clients cancel their own live orders, so it stays bounded.
    constexpr size_t ME_MAX_RESTING_ORDERS = 16 * 1024;
    constexpr size_t ME_MAX_ORDER_PER_CLIENT = 1024 * 64;
    constexpr size_t ME_MAX_PRICE_LEVELS = 256;
    constexpr size_t ME_MAX_PENDING_REQUESTS = 1024;
}