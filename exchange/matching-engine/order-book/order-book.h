#pragma once
#include "common/types.h"
#include "common/constants.h"
#include "common/utils.h"
#include "common/time-utils.h"
#include "common/logging.h"
#include "common/memory-pool.h"
#include "common/perf-utils.h"
#include "exchange/matching-engine/matcher/me-order.h"
#include "exchange/order-server/client-response.h"
#include "exchange/market-data/market-update.h"
#include <array>
#include <bit>
using namespace Common;

namespace Exchange{
    class MatchingEngine;
    class MEOrderBook{
        public:
        MEOrderBook() = delete;
        MEOrderBook(const MEOrderBook &) = delete;
        MEOrderBook(const MEOrderBook &&) = delete;
        MEOrderBook &operator=(const MEOrderBook &) = delete;
        MEOrderBook &operator=(const MEOrderBook &&) = delete;
        MEOrderBook(MatchingEngine *matching_engine, TickerId ticker_id, Logger *logger);
        ~MEOrderBook();
        void add (ClientId client_id, OrderId client_order_id, Side side, Price price, Qty qty) noexcept;
        void cancel (ClientId client_id, OrderId client_order_id) noexcept;
        private:
        MatchingEngine* matching_engine_;
        //ToDo - Improvement - Benchmark
        // Single array of orders. 
        // hold the crossover.
        // Preserve the invariant head of bid < head of ask.
        // Valid price range is [1, ME_MAX_PRICE_LEVELS]. Levels 0 and ME_MAX_PRICE_LEVELS + 1
        // are the sentinel levels (see the level bits below).
        std::array<MEOrdersAtPrice, ME_MAX_PRICE_LEVELS + 2> sell_orders;
        std::array<MEOrdersAtPrice, ME_MAX_PRICE_LEVELS + 2> buy_orders;
        // The only orders on the sentinel levels, linked to themselves. An ask above and a bid below
        // every valid price never match, so the best order is never null: the matching loops stop
        // at them when a side runs out.
        MEOrder ask_sentinel_order_;
        MEOrder bid_sentinel_order_;
        uint64_t head_of_bid_ = 0;
        uint64_t head_of_ask_ = ME_MAX_PRICE_LEVELS + 1;
        // Bit p is set while price level p has orders, used to find the next head when the head
        // level empties. Bit 0 (bids) and bit ME_MAX_PRICE_LEVELS + 1 (asks) are sentinels, always
        // set: the head search always finds a bit, and finding the sentinel means the side is
        // empty, the same values as the empty heads above.
        using LevelBits = std::array<uint64_t, (ME_MAX_PRICE_LEVELS + 1) / 64 + 1>;
        LevelBits ask_levels_{};
        LevelBits bid_levels_{};
        std::array<std::array<MEOrder*,ME_MAX_ORDER_PER_CLIENT>,ME_MAX_NUM_CLIENTS> client_orders_{};
        MemoryPool<MEOrder, ME_MAX_ORDER_IDS> order_memory_pool_;
        TickerId ticker_id_;
        OrderId next_market_order_id_ = 0;
        MEClientResponse client_response_;
        MEMarketUpdate market_update_;
        Logger* logger_ = nullptr;
        std::string time_str_;

        inline auto getNextOrderId() noexcept -> OrderId{
            return next_market_order_id_++;
        }
        //ToDo
        // price has to be validated before here, a price above ME_MAX_PRICE_LEVELS + 1 is past
        // the last word, and a cleared sentinel would make the head search run off the bitmap.
        // An aggressor priced at a sentinel (a BUY at ME_MAX_PRICE_LEVELS + 1, a SELL at 0)
        // would match the sentinel order.
        static inline auto setLevel(LevelBits& levels, Price price) noexcept{
            levels[price / 64] |= 1ull << (price % 64);
        }
        static inline auto clearLevel(LevelBits& levels, Price price) noexcept{
            levels[price / 64] &= ~(1ull << (price % 64));
        }
        inline auto getSellOrdersAtPrice(Price price) noexcept->MEOrdersAtPrice*{
            return &sell_orders[price];
        }
        inline auto getBuyOrdersAtPrice(Price price) noexcept ->MEOrdersAtPrice*{
            return &buy_orders[price];
        }
        // The sentinel order when the side is empty, never null.
        inline auto getBestSellOrder() noexcept -> MEOrder*{
            return sell_orders[head_of_ask_].first_order_;
        }
        inline auto getBestBuyOrder() noexcept -> MEOrder*{
            return buy_orders[head_of_bid_].first_order_;
        }
        void addSellOrder(ClientId client_id, OrderId client_order_id, Price price, Qty qty, OrderId new_market_order_id) noexcept;
        void addBuyOrder(ClientId client_id, OrderId client_order_id, Price price, Qty qty, OrderId new_market_order_id) noexcept;
        void eraseSellOrder(MEOrder* order) noexcept ;
        void eraseBuyOrder(MEOrder* order) noexcept ;
        inline void findNextSellHead()noexcept;
        inline void findNextBuyHead()noexcept;
        Qty remainingFromMatchingBuy(Price price, Qty qty,ClientId client_id, OrderId client_order_id, OrderId new_market_order_id) noexcept;
        Qty remainingFromMatchingSell(Price price, Qty qty,ClientId client_id, OrderId client_order_id, OrderId new_market_order_id) noexcept;
    };
}