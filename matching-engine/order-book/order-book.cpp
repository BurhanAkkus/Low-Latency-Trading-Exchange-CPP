#include "order-book.h"
#include "matcher/matching-engine.h"
using namespace Common;

namespace Exchange{
    MEOrderBook::MEOrderBook(TickerId ticker_id, Logger *logger, MatchingEngine *matching_engine)
        : ticker_id_(ticker_id),
        matching_engine_{matching_engine},
        logger_(logger) {};
    MEOrderBook::~MEOrderBook(){
        logger_->log("Destroying OrderBook for ticker %s at %s",ticker_id_ , getCurrentTimeStr(&time_str_));
        matching_engine_ = nullptr;
        for (auto &itr: client_orders_) {
            itr.clear();
        }
    }
    void MEOrderBook::add (ClientId client_id, OrderId client_order_id, TickerId ticker_id, Side side, Price price, Qty qty) noexcept{
            // send accepted ClientResponse
            const auto new_market_order_id = getNextOrderId();
            client_response_ = {ClientResponseType::ACCEPTED,
                client_id, ticker_id, client_order_id,
                new_market_order_id, side, price, 0, qty};
            matching_engine_->sendClientResponse(&client_response_);
            const auto leaves_qty = checkForMatch(client_id,
                client_order_id, ticker_id, side, price, qty,
                new_market_order_id);
            // order not completely consumed
            if(LIKELY(leaves_qty)){
                // Add to price list
                MEOrdersAtPrice& level = ((side == Side::SELL) ? sellOrders : buyOrders)[price];
                MEOrder* level_head = level.first_order_;
                MEOrder* order;
                if(LIKELY(level_head)){
                    order = order_memory_pool_.allocate(ticker_id, client_id,
                        client_order_id, new_market_order_id, side, price,
                        leaves_qty, level_head, level_head->prev_order_);
                    level_head->prev_order_->next_order_ = order;
                    level_head->prev_order_ = order;
                }
                else{
                    order = order_memory_pool_.allocate(ticker_id, client_id,
                        client_order_id, new_market_order_id, side, price,
                        leaves_qty, nullptr, nullptr);
                    order->next_order_ = order->prev_order_ = order;
                    level.first_order_ = order;
                }
                // update heads
                if(side == Side::SELL){compareAndAssignMin(head_of_ask_,price);}
                else {compareAndAssignMax(head_of_bid_,price);}
                // update client Orders;
                client_orders_[client_id].insert(client_order_id,order);
                // send market update.
                market_update_ = {MarketUpdateType::ADD,
                    new_market_order_id, ticker_id, side, price,
                    leaves_qty};
                matching_engine_->sendMarketUpdate(&market_update_);
            }


        }