#pragma once
#include "common/types.h"
#include "common/constants.h"
#include "common/utils.h"
#include "common/time-utils.h"
#include "common/logging.h"
#include "common/memory-pool.h"
#include "common/perf-utils.h"
#include "matching-engine/matcher/me-order.h"
#include "matching-engine/order-server/client-response.h"
#include "matching-engine/market-data/market-update.h"
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
        std::array<MEOrdersAtPrice, ME_MAX_PRICE_LEVELS + 1> sell_orders;
        std::array<MEOrdersAtPrice, ME_MAX_PRICE_LEVELS + 1> buy_orders;
        // Valid price range is [1, ME_MAX_PRICE_LEVELS]
        uint64_t head_of_bid_ = 0;
        uint64_t head_of_ask_ = ME_MAX_PRICE_LEVELS + 1;
        // Bit (price - 1) is set while that price level has orders: price 1 is bit 0 of word 0,
        // price 256 is bit 63 of word 3. Finds the next head when the head level empties.
        static_assert(ME_MAX_PRICE_LEVELS % 64 == 0, "Price levels must fill whole 64 bit words.");
        using LevelBits = std::array<uint64_t, ME_MAX_PRICE_LEVELS / 64>;
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
        // price has to be validated before here, price 0 would index word -1.
        static inline auto setLevel(LevelBits& levels, Price price) noexcept{
            levels[(price - 1) / 64] |= 1ull << ((price - 1) % 64);
        }
        static inline auto clearLevel(LevelBits& levels, Price price) noexcept{
            levels[(price - 1) / 64] &= ~(1ull << ((price - 1) % 64));
        }
        inline auto getSellOrdersAtPrice(Price price) noexcept->MEOrdersAtPrice*{
            return &sell_orders[price];
        }
        inline auto getBuyOrdersAtPrice(Price price) noexcept ->MEOrdersAtPrice*{
            return &buy_orders[price];
        }
        inline auto getBestSellOrder(){
            return head_of_ask_ <= ME_MAX_PRICE_LEVELS? getSellOrdersAtPrice(head_of_ask_)->first_order_ : nullptr;
        }
        inline auto getBestBuyOrder(){
            return head_of_bid_ > 0? getBuyOrdersAtPrice(head_of_bid_)->first_order_ : nullptr;
        }
        void addSellOrder(ClientId client_id, OrderId client_order_id, Price price, Qty qty, OrderId new_market_order_id) noexcept;
        void addBuyOrder(ClientId client_id, OrderId client_order_id, Price price, Qty qty, OrderId new_market_order_id) noexcept;
        MEOrder* eraseSellOrder(MEOrder* order) noexcept ;
        MEOrder* eraseBuyOrder(MEOrder* order) noexcept ;
        inline void findNextSellHead()noexcept;
        inline void findNextBuyHead()noexcept;
        Qty remainingFromMatchingBuy(Price price, Qty qty,ClientId client_id, OrderId client_order_id, OrderId new_market_order_id) noexcept;
        Qty remainingFromMatchingSell(Price price, Qty qty,ClientId client_id, OrderId client_order_id, OrderId new_market_order_id) noexcept;
    };
}