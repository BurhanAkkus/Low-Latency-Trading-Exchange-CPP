#pragma once
#include "common/types.h"
#include "common/constants.h"
#include <sstream>
#include <map>
#include <array>
using namespace Common;

namespace Exchange{
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