#pragma once
#include "common/types.h"
#include "common/constants.h"
#include "exchange/matching-engine/matcher/me-order.h"
using namespace Common;

namespace Exchange{
    // A book's resting orders by client id and client order id.
    class RestingOrders{
        public:
            // Buckets for every order the book can hold, so they never have to grow later: growing
            // would invalidate the iterators the orders keep.
            RestingOrders(){
                orders_.reserve(ME_MAX_RESTING_ORDERS);
            }
            // The resting order with these ids, nullptr if there is none.
            inline auto find(ClientId client_id, OrderId client_order_id) const noexcept -> MEOrder*{
                const auto it = orders_.find(Key{client_id, client_order_id});
                return it == orders_.end() ? nullptr : it->second;
            }
            // Stores the order's position in the order, for erase().
            inline auto insert(MEOrder* order) noexcept -> void{
                order->resting_position_ = orders_.try_emplace(Key{order->client_id_, order->client_order_id_}, order).first;
            }
            inline auto erase(const MEOrder* order) noexcept -> void{
                orders_.erase(order->resting_position_);
            }
            // The two halves of the warm-up, see MatchingEngine::run(). For an empty book only.
            inline auto fill() noexcept -> void{
                for(OrderId i = 0; i < ME_MAX_RESTING_ORDERS; i++) orders_.try_emplace(Key{0, i}, nullptr);
            }
            inline auto clear() noexcept -> void{
                orders_.clear();
            }
        private:
            using Key = RestingOrderMap::key_type;
            RestingOrderMap orders_;
    };
}
