#include "order-book.h"
#include "matcher/matching-engine.h"
using namespace Common;

namespace Exchange{
    MEOrderBook::MEOrderBook( MatchingEngine *matching_engine, TickerId ticker_id, Logger *logger)
        : ticker_id_(ticker_id),
        matching_engine_{matching_engine},
        logger_(logger) {};
    MEOrderBook::~MEOrderBook(){
        logger_->log("Destroying OrderBook for ticker %s at %s",ticker_id_ , getCurrentTimeStr(&time_str_));
        matching_engine_ = nullptr;
    }
    void MEOrderBook::add(ClientId client_id, OrderId client_order_id, TickerId ticker_id, Side side, Price price, Qty qty) noexcept{
        // send accepted ClientResponse
        const auto new_market_order_id = getNextOrderId();
        client_response_ = {ClientResponseType::ACCEPTED,
            client_id, ticker_id, client_order_id,
            new_market_order_id, side, price, 0, qty};
        matching_engine_->sendClientResponse(&client_response_);
        if(side == Side::SELL){
            addSellOrder(client_id,client_order_id,ticker_id,price,qty, new_market_order_id);
        }else{
            addBuyOrder(client_id,client_order_id,ticker_id,price,qty,new_market_order_id);
        }
    }
    void MEOrderBook::addSellOrder(ClientId client_id, OrderId client_order_id, TickerId ticker_id, Price price, Qty qty, OrderId new_market_order_id) noexcept{
        const auto leaves_qty = remainingFromMatchingSell(price, qty);
        if(LIKELY(leaves_qty)){
            // Add to price list
            MEOrdersAtPrice& level = sell_orders[price];
            MEOrder* level_head = level.first_order_;
            MEOrder* order;
            if(LIKELY(level_head)){
                order = order_memory_pool_.allocate(ticker_id, client_id,
                    client_order_id, new_market_order_id, Side::SELL, price,
                    leaves_qty, level_head, level_head->prev_order_);
                level_head->prev_order_->next_order_ = order;
                level_head->prev_order_ = order;
            }
            else{
                order = order_memory_pool_.allocate(ticker_id, client_id,
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
                new_market_order_id, ticker_id, Side::SELL, price,
                leaves_qty};
            matching_engine_->sendMarketUpdate(&market_update_);
        }
        else{
            client_response_ = {ClientResponseType::FILLED,
                client_id, ticker_id, client_order_id,
                new_market_order_id, Side::SELL, price, qty, 0};
            matching_engine_->sendClientResponse(&client_response_);
        }
    }   

    void MEOrderBook::addBuyOrder(ClientId client_id, OrderId client_order_id, TickerId ticker_id, Price price, Qty qty, OrderId new_market_order_id) noexcept{
        const auto leaves_qty = remainingFromMatchingBuy(price, qty);
        if(LIKELY(leaves_qty)){
            // Add to price list
            MEOrdersAtPrice& level = buy_orders[price];
            MEOrder* level_head = level.first_order_;
            MEOrder* order;
            if(LIKELY(level_head)){
                order = order_memory_pool_.allocate(ticker_id, client_id,
                    client_order_id, new_market_order_id, Side::BUY, price,
                    leaves_qty, level_head, level_head->prev_order_);
                level_head->prev_order_->next_order_ = order;
                level_head->prev_order_ = order;
            }
            else{
                order = order_memory_pool_.allocate(ticker_id, client_id,
                    client_order_id, new_market_order_id, Side::BUY, price,
                    leaves_qty, nullptr, nullptr);
                order->next_order_ = order->prev_order_ = order;
                level.first_order_ = order;
            }
            // update heads
            //ToDo
            // extra conditional to check if book is empty.. 
            // should be similar to sell side, the empty bid should be minimum valid price - 1.
            if (head_of_bid_ == ME_MAX_PRICE_LEVELS || price > head_of_bid_) head_of_bid_ = price;
            // update client Orders;
            client_orders_[client_id][client_order_id] = order;
            // send market update.
            market_update_ = {MarketUpdateType::ADD,
                new_market_order_id, ticker_id, Side::BUY, price,
                leaves_qty};
            matching_engine_->sendMarketUpdate(&market_update_);
        }
        else{
            client_response_ = {ClientResponseType::FILLED,
                client_id, ticker_id, client_order_id,
                new_market_order_id, Side::BUY, price, qty, 0};
            matching_engine_->sendClientResponse(&client_response_);
        }
    }

    // ToDo - Optimize
    // erase all orders at once from a level.
    Qty MEOrderBook::remainingFromMatchingBuy(Price price, Qty qty) noexcept{
        // ToDo
        // Trade off on Likely vs Unlikely.
        // Likely -> pays 1 compare for passive orders
        // Unlikely -> guesses wrong on aggressive orders.
        auto best_offer = getBestSellOrder();
        while(best_offer && best_offer->price_ <= price && qty){
            if(best_offer->qty_ <= qty){
                qty -= best_offer->qty_ ;
                market_update_ = {MarketUpdateType::TRADE,
                    best_offer->market_order_id_, best_offer->ticker_id_, best_offer->side_,
                    best_offer->price_, best_offer->qty_};
                matching_engine_->sendMarketUpdate(&market_update_);
                best_offer = eraseSellOrder(best_offer);
            }
            else{
                best_offer->qty_ -= qty;
                market_update_ = {MarketUpdateType::MODIFY,
                    best_offer->market_order_id_, best_offer->ticker_id_, Side::SELL,
                    best_offer->price_, best_offer->qty_};
                matching_engine_->sendMarketUpdate(&market_update_);
                return 0;
            }
        }
        return qty;
    }
    Qty MEOrderBook::remainingFromMatchingSell(Price price, Qty qty) noexcept{   
        auto best_offer = getBestBuyOrder();
        while(best_offer && best_offer->price_ >= price && qty){
            if(best_offer->qty_ <= qty){
                qty -= best_offer->qty_ ;
                market_update_ = {MarketUpdateType::TRADE,
                    best_offer->market_order_id_, best_offer->ticker_id_, Side::BUY,
                    best_offer->price_, best_offer->qty_};
                matching_engine_->sendMarketUpdate(&market_update_);
                best_offer = eraseBuyOrder(best_offer);
            }
            else{
                best_offer->qty_ -= qty;
                market_update_ = {MarketUpdateType::MODIFY,
                    best_offer->market_order_id_, best_offer->ticker_id_, Side::BUY,
                    best_offer->price_, best_offer->qty_};
                matching_engine_->sendMarketUpdate(&market_update_);
                return 0;
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
        while(head_of_ask_ < ME_MAX_PRICE_LEVELS &&  sell_orders[head_of_ask_].first_order_ == nullptr ){
            head_of_ask_++;
        }
    }
    // ToDo
    // head of bid should be < minimum valid price when empty. 
    inline void MEOrderBook::findNextBuyHead() noexcept {
        head_of_bid_ =  std::min(head_of_bid_ - 1 , ME_MAX_PRICE_LEVELS);
        while(head_of_bid_ < ME_MAX_PRICE_LEVELS && buy_orders[head_of_bid_].first_order_ == nullptr){
            head_of_bid_ =  std::min(head_of_bid_ - 1 , ME_MAX_PRICE_LEVELS);
        }
    }
}