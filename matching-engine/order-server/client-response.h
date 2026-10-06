#include "common/constants.h"
#include "common/types.h"
#include <sstream>
#include <iostream>
#include "common/lf-queue.h"
using namespace Common;

namespace Exchange{
#pragma pack(push,1)
    enum class ClientResponseType:uint8_t{
        INVALID = 0,
        ACCEPTED = 1,
        CANCELLED = 2,
        FILLED = 3,
        CANCEL_REJECTED = 4
    };
    inline std::string clientResponseTypeToString(ClientResponseType clientResponseType) noexcept{
        switch(clientResponseType){
            case ClientResponseType::INVALID:
                return "INVALID";
            case ClientResponseType::ACCEPTED:
                return "ACCEPTED";
            case ClientResponseType::CANCELLED:
                return "CANCELLED";
            case ClientResponseType::FILLED:
                return "FILLED";
            case ClientResponseType::CANCEL_REJECTED:
                return "CANCEL_REJECTED";
        }
        return "UNKNOWN";
    }

    struct MEClientResponse{
        ClientResponseType type_{ClientResponseType::INVALID};
        ClientId client_id_{ClientId_INVALID};
        TickerId ticker_id_{TickerId_INVALID};
        OrderId client_order_id_{OrderId_INVALID}; 
        OrderId market_order_id_{OrderId_INVALID};
        Side side_{Side::INVALID};
        Price price_{Price_INVALID};
        Qty exec_qty_{Qty_INVALID};
        Qty left_qty_{Qty_INVALID};

        inline std::string toString() const noexcept{
            std::stringstream ss;
            ss << "MEClientResponse";
            ss << "[";
            ss <<"type: " << clientResponseTypeToString(type_);
            ss <<" clientId: " << clientIdToString(client_id_);
            ss <<" tickerId: " << tickerIdToString(ticker_id_);
            ss <<" clientOrderId: " << orderIdToString(client_order_id_);
            ss <<" marketOrderId: " << orderIdToString(market_order_id_);
            ss <<" side: " << sideToString(side_);
            ss <<" price: " << priceToString(price_);
            ss <<" executedQuantity: " << qtyToString(exec_qty_);
            ss <<" leftQuantity: " << qtyToString(left_qty_);
            ss << "]";
            return ss.str();
        }
    };
#pragma pack(pop)
using ClientResponseLFQueue = LFQueue<MEClientResponse>;
}