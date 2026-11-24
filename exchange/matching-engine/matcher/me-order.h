#pragma once
#include "common/types.h"
#include "common/constants.h"
#include <sstream>
#include <map>
#include <array>
#include <unordered_map>
using namespace Common;

namespace Exchange{
  struct MEOrder;
  // A book's resting orders by client id and client order id, see RestingOrders. Declared here
  // because every order keeps its iterator.
  struct RestingOrderHash{
    auto operator()(const std::pair<ClientId,OrderId>& key) const noexcept -> std::size_t{
      return key.second * ME_MAX_NUM_CLIENTS + key.first;
    }
  };
  using RestingOrderMap = std::unordered_map<std::pair<ClientId,OrderId>, MEOrder*, RestingOrderHash>;
// ToDo 
// align objects to 64 bytes.
    struct MEOrder {
    TickerId ticker_id_ = TickerId_INVALID;
    ClientId client_id_ = ClientId_INVALID;
    OrderId client_order_id_ = OrderId_INVALID;
    OrderId market_order_id_ = OrderId_INVALID;
    Side side_ = Side::INVALID;
    Price price_ = Price_INVALID;
    Qty qty_ = Qty_INVALID;
    MEOrder* next_order_ = nullptr;
    MEOrder* prev_order_ = nullptr;
    // Where this order is in its book's resting orders, set when it starts resting.
    RestingOrderMap::iterator resting_position_{};
    MEOrder() = default;
    MEOrder(TickerId ticker_id, ClientId client_id, OrderId
      client_order_id, OrderId market_order_id, Side side,
      Price price,Qty qty,MEOrder *next_order, MEOrder *prev_order) noexcept
        :     ticker_id_(ticker_id),
              client_id_(client_id),
              client_order_id_(client_order_id),
              market_order_id_(market_order_id),
              side_(side),
              price_(price),
              qty_(qty),
              next_order_{next_order},
              prev_order_{prev_order} {}
    auto toString() const -> std::string;
  };
  struct MEOrdersAtPrice{
    MEOrder* first_order_ = nullptr;
  };
  //ToDo
  // implement allocators utilizing memoryPool.
  using OrderHashMap = std::array<MEOrder*,ME_MAX_ORDER_PER_CLIENT>;
  using OrdersAtPriceMap = std::map<Price,MEOrdersAtPrice*>;
}