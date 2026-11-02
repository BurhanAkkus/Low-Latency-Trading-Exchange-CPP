#pragma once
#include "common/shared-memory.h"
#include "exchange/market-data/market-update.h"

namespace Exchange{
    // The incremental market data in shared memory, in place of the multicast socket: the exchange
    // creates it (common/shared-region.h), the publisher writes, one reader (the benchmark)
    // attaches. A read returns whole updates only.
    constexpr const char* MARKET_DATA_CHANNEL_NAME = "/exchange-market-data";
    // ~120k updates/s at 42 bytes: 4 MB holds about 0.8 s of them.
    constexpr size_t MARKET_DATA_RING_SIZE = 4 * 1024 * 1024;

    struct MarketDataChannel{
        SharedMemory<MARKET_DATA_RING_SIZE, sizeof(MDPMarketUpdate)> incremental_;
    };
}
