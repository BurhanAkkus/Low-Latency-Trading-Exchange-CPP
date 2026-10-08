#include "common/types.h"
#include "common/constants.h"
#include "common/utils.h"
#include "matcher/me-order.h"
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
        MEOrderBook(TickerId ticker_id, Logger *logger, MatchingEngine *matching_engine);
        ~MEOrderBook();
        private:
        MatchingEngine* matching_engine_;
        //ToDo - Improvement - Benchmark
        // Single array of orders. 
        // hold the crossover.
        // Preserve the invariant head of bid < head of ask.
        std::array<MEOrdersAtPrice, ME_MAX_PRICE_LEVELS> sellOrders;
        std::array<MEOrdersAtPrice, ME_MAX_PRICE_LEVELS> buyOrders;
        int head_of_bid_ = -1;
        int head_of_ask_ = ME_MAX_PRICE_LEVELS;
        std::array<OrderHashMap,ME_MAX_NUM_CLIENTS> client_orders_;
        MemoryPool<MEOrder> order_memory_pool_{ME_MAX_ORDER_IDS};
        TickerId ticker_id_;
        OrderId next_market_order_id_;
        MEClientResponse client_response_;
        MEMarketUpdate market_update_;
        Logger* logger_ = nullptr;
        std::string time_str_;

        auto getNextOrderId() noexcept -> OrderId{
            return next_market_order_id_++;
        }
        //ToDo
        // price has to be validated before here.
        auto getSellOrdersAtPrice(Price price) noexcept->MEOrdersAtPrice*{
            return &sellOrders[price];
        }
        auto getBuyOrdersAtPrice(Price price) noexcept ->MEOrdersAtPrice*{
            return &buyOrders[price];
        }
        void MEOrderBook::add (ClientId client_id, OrderId client_order_id, TickerId ticker_id, Side side, Price price, Qty qty) noexcept;
    
    };
}