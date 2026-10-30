#include "order-book.h"
#include "exchange/matching-engine/matcher/matching-engine.h"
using namespace Common;

namespace Exchange{
    MEOrderBook::MEOrderBook(MatchingEngine *matching_engine, TickerId ticker_id, Logger *logger)
        : matching_engine_{matching_engine},
        ticker_id_(ticker_id),
        logger_(logger) {
            ask_sentinel_order_.price_ = ME_MAX_PRICE_LEVELS + 1;
            ask_sentinel_order_.next_order_ = ask_sentinel_order_.prev_order_ = &ask_sentinel_order_;
            sell_orders[ME_MAX_PRICE_LEVELS + 1].first_order_ = &ask_sentinel_order_;
            setLevel(ask_levels_, ME_MAX_PRICE_LEVELS + 1);
            bid_sentinel_order_.price_ = 0;
            bid_sentinel_order_.next_order_ = bid_sentinel_order_.prev_order_ = &bid_sentinel_order_;
            buy_orders[0].first_order_ = &bid_sentinel_order_;
            setLevel(bid_levels_, 0);
        };
    MEOrderBook::~MEOrderBook(){
        logger_->log("Destroying OrderBook for ticker % at %",ticker_id_ , getCurrentTimeStr(&time_str_));
        matching_engine_ = nullptr;
    }
    void MEOrderBook::add(ClientId client_id, OrderId client_order_id, Side side, Price price, Qty qty) noexcept{
        // ToDo - improve
        // Massive array lookup! Super sparse. employ a better suited sparse container.

        // send accepted ClientResponse
        if(client_orders_[client_id][client_order_id] != nullptr){
            client_response_ = {ClientResponseType::REJECTED,
                client_id, ticker_id_, client_order_id,
                OrderId_INVALID, side, price, 0, 0};
            matching_engine_->sendClientResponse(&client_response_);
            return;
        }
        const auto new_market_order_id = getNextOrderId();
        client_response_ = {ClientResponseType::ACCEPTED,
            client_id, ticker_id_, client_order_id,
            new_market_order_id, side, price, 0, qty};
        matching_engine_->sendClientResponse(&client_response_);
        if(side == Side::SELL){
            addSellOrder(client_id,client_order_id,price,qty, new_market_order_id);
        }else{
            addBuyOrder(client_id,client_order_id,price,qty,new_market_order_id);
        }
    }
    
    void MEOrderBook::addSellOrder(ClientId client_id, OrderId client_order_id,  Price price, Qty qty, OrderId new_market_order_id) noexcept{
        // START_MEASURE(Exchange_MEOrderBook_checkForMatch);
        const auto leaves_qty = remainingFromMatchingSell(price, qty, client_id, client_order_id, new_market_order_id);
        // END_MEASURE(Exchange_MEOrderBook_checkForMatch, (*logger_));
        if(LIKELY(leaves_qty)){
            // START_MEASURE(Exchange_MEOrderBook_addOrder);
            // Add to price list
            MEOrdersAtPrice& level = sell_orders[price];
            MEOrder* level_head = level.first_order_;
            MEOrder* order;
            if(LIKELY(level_head)){
                order = order_memory_pool_.allocate(ticker_id_, client_id,
                    client_order_id, new_market_order_id, Side::SELL, price,
                    leaves_qty, level_head, level_head->prev_order_);
                level_head->prev_order_->next_order_ = order;
                level_head->prev_order_ = order;
            }
            else{
                order = order_memory_pool_.allocate(ticker_id_, client_id,
                    client_order_id, new_market_order_id, Side::SELL, price,
                    leaves_qty, nullptr, nullptr);
                order->next_order_ = order->prev_order_ = order;
                level.first_order_ = order;
                setLevel(ask_levels_, price);
            }
            // update heads
            compareAndAssignMin(head_of_ask_,price);
            // update client Orders;
            client_orders_[client_id][client_order_id] = order;
            // END_MEASURE(Exchange_MEOrderBook_addOrder, (*logger_));
            // send market update.
            market_update_ = {MarketUpdateType::ADD,
                new_market_order_id, ticker_id_, Side::SELL, price,
                leaves_qty};
            matching_engine_->sendMarketUpdate(&market_update_);
        }
    }   
    void MEOrderBook::addBuyOrder(ClientId client_id, OrderId client_order_id, Price price, Qty qty, OrderId new_market_order_id) noexcept{
        // START_MEASURE(Exchange_MEOrderBook_checkForMatch);
        const auto leaves_qty = remainingFromMatchingBuy(price, qty, client_id, client_order_id, new_market_order_id);
        // END_MEASURE(Exchange_MEOrderBook_checkForMatch, (*logger_));
        if(LIKELY(leaves_qty)){
            // START_MEASURE(Exchange_MEOrderBook_addOrder);
            // Add to price list
            MEOrdersAtPrice& level = buy_orders[price];
            MEOrder* level_head = level.first_order_;
            MEOrder* order;
            if(LIKELY(level_head)){
                order = order_memory_pool_.allocate(ticker_id_, client_id,
                    client_order_id, new_market_order_id, Side::BUY, price,
                    leaves_qty, level_head, level_head->prev_order_);
                level_head->prev_order_->next_order_ = order;
                level_head->prev_order_ = order;
            }
            else{
                order = order_memory_pool_.allocate(ticker_id_, client_id,
                    client_order_id, new_market_order_id, Side::BUY, price,
                    leaves_qty, nullptr, nullptr);
                order->next_order_ = order->prev_order_ = order;
                level.first_order_ = order;
                setLevel(bid_levels_, price);
            }
            //update head
            if (price > head_of_bid_) head_of_bid_ = price;
            // update client Orders;
            client_orders_[client_id][client_order_id] = order;
            // END_MEASURE(Exchange_MEOrderBook_addOrder, (*logger_));
            // send market update.
            market_update_ = {MarketUpdateType::ADD,
                new_market_order_id, ticker_id_, Side::BUY, price,
                leaves_qty};
            matching_engine_->sendMarketUpdate(&market_update_);
        }
    }

    // ToDo - Optimize
    // erase all orders at once from a level.
    Qty MEOrderBook::remainingFromMatchingBuy(Price price, Qty qty,ClientId client_id, OrderId client_order_id, OrderId new_market_order_id) noexcept{
        auto best_offer = getBestSellOrder();
        auto tail = best_offer->prev_order_;
        while(best_offer->price_ <= price && qty){
            // START_MEASURE(Exchange_MEOrderBook_match);
            auto fill = std::min(qty,best_offer->qty_);
            qty -= fill;
            best_offer->qty_ -= fill;
            // send client responses.
            // aggressor
            client_response_ = {ClientResponseType::FILLED,
                client_id, ticker_id_, client_order_id,
                new_market_order_id, Side::BUY, best_offer->price_, fill, qty };
            matching_engine_->sendClientResponse(&client_response_);
            // passive
            client_response_ = {ClientResponseType::FILLED,
                best_offer->client_id_, ticker_id_, best_offer->client_order_id_,
                best_offer ->market_order_id_ , Side::SELL, best_offer->price_, fill, best_offer->qty_};
            matching_engine_->sendClientResponse(&client_response_);
            // Trade Happened
            market_update_ = {MarketUpdateType::TRADE,
                OrderId_INVALID, best_offer->ticker_id_, Side::BUY,
                best_offer->price_, fill};
            matching_engine_->sendMarketUpdate(&market_update_);

            // Passive order completely filled.
            if(best_offer->qty_ == 0){
                market_update_ = {MarketUpdateType::CANCEL,
                    best_offer->market_order_id_, ticker_id_, Side::SELL,
                    best_offer->price_, 0};
                matching_engine_->sendMarketUpdate(&market_update_);
                // START_MEASURE(Exchange_MEOrderBook_removeOrder);
                client_orders_[best_offer->client_id_][best_offer->client_order_id_] = nullptr;
                // Level completely consumed.
                if(UNLIKELY(best_offer == tail)){
                    sell_orders[best_offer->price_].first_order_ = nullptr;
                    clearLevel(ask_levels_, best_offer->price_);
                    order_memory_pool_.deallocate(best_offer);
                    findNextSellHead();
                    best_offer = getBestSellOrder();
                    tail = best_offer->prev_order_;
                }
                // There are still left in the level.
                else{
                    best_offer = best_offer->next_order_;
                    order_memory_pool_.deallocate(best_offer->prev_order_);
                }
                // END_MEASURE(Exchange_MEOrderBook_removeOrder, (*logger_));
            }
            else{
                market_update_ = {MarketUpdateType::MODIFY,
                    best_offer->market_order_id_, ticker_id_, Side::SELL,
                    best_offer->price_,best_offer->qty_ };
                matching_engine_->sendMarketUpdate(&market_update_);
            }
            // END_MEASURE(Exchange_MEOrderBook_match, (*logger_));
        }
        // A sentinel order relinks to itself.
        sell_orders[best_offer->price_].first_order_ = best_offer;
        best_offer->prev_order_ = tail;
        tail->next_order_ = best_offer;
        return qty;
    }
    
    Qty MEOrderBook::remainingFromMatchingSell(Price price, Qty qty,ClientId client_id, OrderId client_order_id, OrderId new_market_order_id) noexcept{   
        auto best_offer = getBestBuyOrder();
        auto tail = best_offer->prev_order_;
        while(best_offer->price_ >= price && qty){
            // START_MEASURE(Exchange_MEOrderBook_match);
            auto fill = std::min(qty,best_offer->qty_);
            qty -= fill;
            best_offer->qty_ -= fill;
            // send client responses.
            // aggressor
            client_response_ = {ClientResponseType::FILLED,
                client_id, ticker_id_, client_order_id,
                new_market_order_id, Side::SELL, best_offer->price_, fill, qty };
            matching_engine_->sendClientResponse(&client_response_);
            // passive
            client_response_ = {ClientResponseType::FILLED,
                best_offer->client_id_, ticker_id_, best_offer->client_order_id_,
                best_offer ->market_order_id_ , Side::BUY, best_offer->price_, fill, best_offer->qty_};
            matching_engine_->sendClientResponse(&client_response_);
            // Trade Happened
            market_update_ = {MarketUpdateType::TRADE,
                OrderId_INVALID, best_offer->ticker_id_, Side::SELL,
                best_offer->price_, fill};
            matching_engine_->sendMarketUpdate(&market_update_);

            // Passive order completely filled.
            if(best_offer->qty_ == 0){
                market_update_ = {MarketUpdateType::CANCEL,
                    best_offer->market_order_id_, ticker_id_, Side::BUY,
                    best_offer->price_, 0};
                matching_engine_->sendMarketUpdate(&market_update_);
                // START_MEASURE(Exchange_MEOrderBook_removeOrder);
                client_orders_[best_offer->client_id_][best_offer->client_order_id_] = nullptr;
                // Level completely consumed.
                if(UNLIKELY(best_offer == tail)){
                    buy_orders[best_offer->price_].first_order_ = nullptr;
                    clearLevel(bid_levels_, best_offer->price_);
                    order_memory_pool_.deallocate(best_offer);
                    findNextBuyHead();
                    best_offer = getBestBuyOrder();
                    tail = best_offer->prev_order_;
                }
                // There are still left in the level.
                else{
                    best_offer = best_offer->next_order_;
                    order_memory_pool_.deallocate(best_offer->prev_order_);
                }
                // END_MEASURE(Exchange_MEOrderBook_removeOrder, (*logger_));
            }
            else{
                market_update_ = {MarketUpdateType::MODIFY,
                    best_offer->market_order_id_, ticker_id_, Side::BUY,
                    best_offer->price_,best_offer->qty_ };
                matching_engine_->sendMarketUpdate(&market_update_);
            }
            // END_MEASURE(Exchange_MEOrderBook_match, (*logger_));
        }
        // A sentinel order relinks to itself.
        buy_orders[best_offer->price_].first_order_ = best_offer;
        best_offer->prev_order_ = tail;
        tail->next_order_ = best_offer;
        return qty;
    }


    void MEOrderBook::cancel(ClientId client_id, OrderId client_order_id) noexcept{
        auto order = client_orders_[client_id][client_order_id];
        if(UNLIKELY(order == nullptr)){
            client_response_ = {ClientResponseType::CANCEL_REJECTED,
                client_id, ticker_id_,client_order_id,
                OrderId_INVALID , Side::INVALID, Price_INVALID, Qty_INVALID, Qty_INVALID};
            matching_engine_->sendClientResponse(&client_response_);
            return;
        }
        client_response_ = {ClientResponseType::CANCELED,
            order->client_id_, ticker_id_, order->client_order_id_,
            order ->market_order_id_ , order->side_, order->price_, 0, 0};
        matching_engine_->sendClientResponse(&client_response_);
        market_update_ = {MarketUpdateType::CANCEL,
            order->market_order_id_,ticker_id_, order->side_,
            order->price_, 0};
        matching_engine_->sendMarketUpdate(&market_update_);
        // START_MEASURE(Exchange_MEOrderBook_removeOrder);
        if(order->side_ == Side::BUY){ 
            eraseBuyOrder(order);
        }
        else{
            eraseSellOrder(order);
        }
        // END_MEASURE(Exchange_MEOrderBook_removeOrder, (*logger_));
    }
        
    // Removes a cancelled order from its level, keeps the level bits and the head up to date.
    void MEOrderBook::eraseSellOrder(MEOrder* order) noexcept {
        // remove from client orders.
        client_orders_[order->client_id_][order->client_order_id_] = nullptr;
        auto& level = sell_orders[order->price_];
        // only order left at price
        if(order->next_order_ == order){
            level.first_order_ = nullptr;
            clearLevel(ask_levels_, order->price_);
            if(UNLIKELY( head_of_ask_== order->price_)){findNextSellHead();}
        }
        else{
            order->next_order_->prev_order_ = order->prev_order_;
            order->prev_order_->next_order_ = order->next_order_;
            if(level.first_order_ == order){
                level.first_order_ = order->next_order_;
            }
        }
        // deallocate order.
        order_memory_pool_.deallocate(order);
    }
    void MEOrderBook::eraseBuyOrder(MEOrder* order) noexcept {
        // remove from client orders.
        client_orders_[order->client_id_][order->client_order_id_] = nullptr;
        auto& level = buy_orders[order->price_];
        // only order left at price
        if(order->next_order_ == order){
            level.first_order_ = nullptr;
            clearLevel(bid_levels_, order->price_);
            if(UNLIKELY(head_of_bid_ == order->price_)){findNextBuyHead();}
        }
        else{
            order->next_order_->prev_order_ = order->prev_order_;
            order->prev_order_->next_order_ = order->next_order_;
            if(level.first_order_ == order){
                level.first_order_ = order->next_order_;
            }
        }
        // deallocate order.
        order_memory_pool_.deallocate(order);
    }

    //ToDo
    // Linear scan vs bitmap scan
    // The old head level just emptied, every other ask is above it: the new head is the lowest
    // set bit from the head's word up. The sentinel ends the search, ME_MAX_PRICE_LEVELS + 1
    // if there are no asks left.
    // A/B experiment 6: linear scan up the levels, as before 0ce11e6. The sentinel level is never
    // empty and ends the scan.
    inline void MEOrderBook::findNextSellHead() noexcept {
        do head_of_ask_++; while (!sell_orders[head_of_ask_].first_order_);
    }
    // Every other bid is below the old head: highest set bit from the head's word down, the
    // sentinel ends the search at 0 if there are no bids left.
    // A/B experiment 6: linear scan down the levels, as before 0ce11e6, the sentinel ends it.
    inline void MEOrderBook::findNextBuyHead() noexcept {
        do head_of_bid_--; while (!buy_orders[head_of_bid_].first_order_);
    }
}