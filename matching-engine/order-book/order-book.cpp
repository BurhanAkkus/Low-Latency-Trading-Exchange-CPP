#include "order-book.h"
#include "matching-engine/matcher/matching-engine.h"
using namespace Common;

namespace Exchange{
    MEOrderBook::MEOrderBook(MatchingEngine *matching_engine, TickerId ticker_id, Logger *logger)
        : matching_engine_{matching_engine},
        ticker_id_(ticker_id),
        logger_(logger) {};
    MEOrderBook::~MEOrderBook(){
        logger_->log("Destroying OrderBook for ticker % at %",ticker_id_ , getCurrentTimeStr(&time_str_));
        matching_engine_ = nullptr;
    }
    void MEOrderBook::add(ClientId client_id, OrderId client_order_id, Side side, Price price, Qty qty) noexcept{
        // send accepted ClientResponse
        if(client_orders_[client_id][client_order_id] != nullptr){
            client_response_ = {ClientResponseType::CANCELLED,
                client_id, ticker_id_, client_order_id,
                OrderId_INVALID, side, price, 0, qty};
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
        const auto leaves_qty = remainingFromMatchingSell(price, qty, client_id, client_order_id, new_market_order_id);
        if(LIKELY(leaves_qty)){
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
            }
            // update heads
            compareAndAssignMin(head_of_ask_,price);
            // update client Orders;
            client_orders_[client_id][client_order_id] = order;
            // send market update.
            market_update_ = {MarketUpdateType::ADD,
                new_market_order_id, ticker_id_, Side::SELL, price,
                leaves_qty};
            matching_engine_->sendMarketUpdate(&market_update_);
        }
    }   

    void MEOrderBook::addBuyOrder(ClientId client_id, OrderId client_order_id, Price price, Qty qty, OrderId new_market_order_id) noexcept{
        const auto leaves_qty = remainingFromMatchingBuy(price, qty, client_id, client_order_id, new_market_order_id);
        if(LIKELY(leaves_qty)){
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
            }
            //update head
            if (price > head_of_bid_) head_of_bid_ = price;
            // update client Orders;
            client_orders_[client_id][client_order_id] = order;
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
        // ToDo
        // Trade off on Likely vs Unlikely.
        // Likely -> pays 1 compare for passive orders
        // Unlikely -> guesses wrong on aggressive orders.
        auto best_offer = getBestSellOrder();
        while(best_offer && best_offer->price_ <= price && qty){
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
                best_offer = eraseSellOrder(best_offer);
            }
            else{
                market_update_ = {MarketUpdateType::MODIFY,
                    best_offer->market_order_id_, ticker_id_, Side::SELL,
                    best_offer->price_,best_offer->qty_ };
                matching_engine_->sendMarketUpdate(&market_update_);
            }
        }
        return qty;
    }
    
    Qty MEOrderBook::remainingFromMatchingSell(Price price, Qty qty,ClientId client_id, OrderId client_order_id, OrderId new_market_order_id) noexcept{   
        auto best_offer = getBestBuyOrder();
        while(best_offer && best_offer->price_ >= price && qty){
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
                best_offer = eraseBuyOrder(best_offer);
            }
            else{
                market_update_ = {MarketUpdateType::MODIFY,
                    best_offer->market_order_id_, ticker_id_, Side::BUY,
                    best_offer->price_,best_offer->qty_ };
                matching_engine_->sendMarketUpdate(&market_update_);
            }
        }
        return qty;
    }

    // Preserves head
    MEOrder* MEOrderBook::eraseSellOrder(MEOrder* order) noexcept {
        // remove from client orders.
        client_orders_[order->client_id_][order->client_order_id_] = nullptr;
        auto& level = sell_orders[order->price_];
        // only order left at price
        MEOrder* next_order;
        if(UNLIKELY(order->next_order_ == order)){
            level.first_order_ = nullptr;
            // find next head.
            findNextSellHead();
            next_order = getBestSellOrder();
        }
        else{
            next_order = order -> next_order_;
            next_order->prev_order_ = order->prev_order_;
            order->prev_order_->next_order_ = next_order;
            if(level.first_order_ == order){
                level.first_order_ = next_order;
            }
        }
        // deallocate order.
        order_memory_pool_.deallocate(order);
        return next_order;
    }

    MEOrder* MEOrderBook::eraseBuyOrder(MEOrder* order) noexcept {
        // remove from client orders.
        client_orders_[order->client_id_][order->client_order_id_] = nullptr;
        auto& level = buy_orders[order->price_];
        // only order left at price
        MEOrder* next_order;
        if(UNLIKELY(order->next_order_ == order)){
            level.first_order_ = nullptr;
            findNextBuyHead();
            next_order = getBestBuyOrder();
        }
        else{
            next_order = order -> next_order_;
            next_order->prev_order_ = order->prev_order_;
            order->prev_order_->next_order_ = next_order;
            if(level.first_order_ == order){
                level.first_order_ = next_order;
            }
        }
        // deallocate order.
        order_memory_pool_.deallocate(order);
        return next_order;
    }

    inline void MEOrderBook::findNextSellHead() noexcept {
        head_of_ask_++;
        while(head_of_ask_ <= ME_MAX_PRICE_LEVELS &&  sell_orders[head_of_ask_].first_order_ == nullptr ){
            head_of_ask_++;
        }
    }
    // ToDo
    // head of bid should be < minimum valid price when empty. 
    inline void MEOrderBook::findNextBuyHead() noexcept {
        head_of_bid_ --;
        while(head_of_bid_ > 0 &&  buy_orders[head_of_bid_].first_order_ == nullptr){
            head_of_bid_--;
        }
    }
}