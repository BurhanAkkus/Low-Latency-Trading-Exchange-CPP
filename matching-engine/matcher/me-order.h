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
    Priority priority_ = Priority_INVALID;
    MEOrder* next_order_ = nullptr;
    MEOrder* prev_order_ = nullptr;
    MEOrder() = default;
    MEOrder(TickerId ticker_id, ClientId client_id, OrderId
      client_order_id, OrderId market_order_id, Side side,
      Price price,Qty qty, Priority priority, MEOrder
      *prev_order, MEOrder *next_order) noexcept
        :     ticker_id_(ticker_id),
              client_id_(client_id),
              client_order_id_(client_order_id),
              market_order_id_(market_order_id),
              side_(side),
              price_(price),
              qty_(qty),
              priority_(priority) {}
    auto toString() const -> std::string;
  };
  struct MEOrdersAtPrice{
    Price price_ = Price_INVALID;
    MEOrder* first_order_ = nullptr;
  };
  struct MEOrdersAtPriceList{
    Side side_ = Side::INVALID;
    std::array<MEOrdersAtPrice, ME_MAX_PRICE_LEVELS> price_levels_;
  };
  //ToDo
  // implement allocators utilizing memoryPool.
  using OrderHashMap = std::map<OrderId,MEOrder*>;
  using ClientOrderHashMap = std::map<ClientId,OrderHashMap>;
  using OrdersAtPriceMap = std::map<Price,MEOrdersAtPrice*>;
}