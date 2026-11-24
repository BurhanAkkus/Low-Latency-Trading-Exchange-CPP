#pragma once
#include "common/types.h"
#include "common/constants.h"
#include "exchange/matching-engine/matcher/me-order.h"
#include <array>
using namespace Common;

namespace Exchange{
    // A book's resting orders by client id and client order id.
    class RestingOrders{
        public:
            // The resting order with these ids, nullptr if there is none.
            inline auto find(ClientId client_id, OrderId client_order_id) const noexcept -> MEOrder*{
                return orders_[client_id][client_order_id];
            }
            inline auto insert(MEOrder* order) noexcept -> void{
                orders_[order->client_id_][order->client_order_id_] = order;
            }
            inline auto erase(const MEOrder* order) noexcept -> void{
                orders_[order->client_id_][order->client_order_id_] = nullptr;
            }
        private:
            std::array<std::array<MEOrder*,ME_MAX_ORDER_PER_CLIENT>,ME_MAX_NUM_CLIENTS> orders_{};
    };
}
