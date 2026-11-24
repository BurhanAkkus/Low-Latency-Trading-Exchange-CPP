#pragma once
#include "common/types.h"
#include "common/constants.h"
#include "exchange/matching-engine/matcher/me-order.h"
#include <unordered_map>
#include <utility>
using namespace Common;

namespace Exchange{
    // A book's resting orders by client id and client order id.
    class RestingOrders{
        public:
            // Buckets for every order the book can hold, so they never have to grow later.
            RestingOrders(){
                orders_.reserve(ME_MAX_RESTING_ORDERS);
            }
            // The resting order with these ids, nullptr if there is none.
            inline auto find(ClientId client_id, OrderId client_order_id) const noexcept -> MEOrder*{
                const auto it = orders_.find(Key{client_id, client_order_id});
                return it == orders_.end() ? nullptr : it->second;
            }
            inline auto insert(MEOrder* order) noexcept -> void{
                orders_.try_emplace(Key{order->client_id_, order->client_order_id_}, order);
            }
            inline auto erase(const MEOrder* order) noexcept -> void{
                orders_.erase(Key{order->client_id_, order->client_order_id_});
            }
            // The two halves of the warm-up, see MatchingEngine::run(). For an empty book only.
            inline auto fill() noexcept -> void{
                for(OrderId i = 0; i < ME_MAX_RESTING_ORDERS; i++) orders_.try_emplace(Key{0, i}, nullptr);
            }
            inline auto clear() noexcept -> void{
                orders_.clear();
            }
        private:
            using Key = std::pair<ClientId, OrderId>;
            struct KeyHash{
                auto operator()(const Key& key) const noexcept -> std::size_t{
                    return key.second * ME_MAX_NUM_CLIENTS + key.first;
                }
            };
            std::unordered_map<Key, MEOrder*, KeyHash> orders_;
    };
}
