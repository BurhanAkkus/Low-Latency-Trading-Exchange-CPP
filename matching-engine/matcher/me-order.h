#include "common/types.h"
#include "common/constants.h"
#include <sstream>
#include <map>
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
  struct MEOrderLinkedListNode{
    MEOrder meOrder;
    MEOrderLinkedListNode* next = nullptr;
    MEOrderLinkedListNode* prev = nullptr;
  };
  struct MEOrdersAtPrice{
    Price price_ = Price_INVALID;
    MEOrderLinkedListNode* head = nullptr; 
    MEOrderLinkedListNode* tail = nullptr; 
  };
  struct MEOrdersAtPriceLinkedListNode{
    MEOrdersAtPrice meOrdersAtPrice;
    MEOrdersAtPriceLinkedListNode* next;
    MEOrdersAtPriceLinkedListNode* prev;
  };
  struct MEOrdersAtPriceLinkedList{
    Side side_ = Side::INVALID;
    MEOrdersAtPriceLinkedListNode* head = nullptr;
    MEOrdersAtPriceLinkedListNode* tail = nullptr;
  }
  //ToDo
  // Implement Hashmap<OrderId, MEOrder*>
  using OrderHashMap = std::map<OrderId,MEOrder *>;
  //ToDo
  // Implement Hashmap<CustomerId, OrderHashMap>
  using ClientOrderHashMap = std::map<ClientId,OrderHashMap>;
}