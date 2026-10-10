#pragma once
#include "common/types.h"
#include "common/macros.h"
#include "common/constants.h"
#include "common/lf-queue.h"
#include <sstream>
using namespace Common;

namespace Exchange{
#pragma pack(push,1) // No padding in types we'll push to wire.
    enum class ClientRequestType:uint8_t{
        INVALID = 0,
        NEW = 1,
        CANCEL = 2
        // No Modify, Modify will be simulated by cancel + new.
    };

    inline std::string clientREquestTypeToString(ClientRequestType clientRequestType) noexcept{
        switch(clientRequestType){
            case ClientRequestType::INVALID:
                return "INVALID";
            case ClientRequestType::NEW:
                return "NEW";
            case ClientRequestType::CANCEL:
                return "CANCEL";
        }
        return "UNKNOWN";
    }

    struct MEClientRequest{
        ClientRequestType type_ = ClientRequestType::INVALID;
        OrderId order_id_ = OrderId_INVALID;
        TickerId tickerId_ = TickerId_INVALID;
        ClientId clientId_ = ClientId_INVALID;
        Price price_ = Price_INVALID;
        Qty qty_ = Qty_INVALID;
        Side side_ = Side::INVALID;
        auto toString() const noexcept{
            std::stringstream ss;
            ss << "MEClientRequest";
            ss << "[";
            ss << "type: " << clientREquestTypeToString(type_);
            ss << " orderId: " << orderIdToString(order_id_);
            ss << " tickerId: " << tickerIdToString(tickerId_);
            ss << " clientId: " << clientIdToString(clientId_);
            ss << " price: " << priceToString(price_);
            ss << " quantity: " << qtyToString(qty_);
            ss << " side: " << sideToString(side_);
            ss << "]";
            return ss.str();
        }
    };
#pragma pack(pop)
using ClientRequestLFQueue = LFQueue<MEClientRequest>;
}