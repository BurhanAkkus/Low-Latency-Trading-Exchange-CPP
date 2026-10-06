#include "common/constants.h"
#include "common/types.h"
#include "common/lf-queue.h"
#include <sstream>
using namespace Common;

namespace Exchange{
#pragma pack(push,1)

    enum class MarketUpdateType:uint8_t{
        INVALID = 0,
        ADD = 1,
        MODIFY = 2,
        CANCEL = 3,
        TRADE = 4
    };
    inline std::string marketUpdateTypeToString(MarketUpdateType marketUpdateType) noexcept{
        switch(marketUpdateType){
            case MarketUpdateType::INVALID:
                return "INVALID";
            case MarketUpdateType::ADD:
                return "ADD";
            case MarketUpdateType::MODIFY:
                return "MODIFY";
            case MarketUpdateType::CANCEL:
                return "CANCEL";
            case MarketUpdateType::TRADE:
                return "TRADE";
        }
        return "UNKNOWN";
    }

    struct MEMarketUpdate{
        MarketUpdateType type_{MarketUpdateType::INVALID};
        OrderId order_id_{OrderId_INVALID};
        TickerId ticker_id_{TickerId_INVALID};
        Side side_{Side::INVALID};
        Price price_{Price_INVALID};
        Qty qty_{Qty_INVALID};
        Priority priority_{Priority_INVALID};
        auto toString() const {
            std::stringstream ss;
            ss << "MEMarketUpdate"
                << " ["
                << " type:" << marketUpdateTypeToString(type_)
                << " ticker:" << tickerIdToString(ticker_id_)
                << " oid:" << orderIdToString(order_id_)
                << " side:" << sideToString(side_)
                << " qty:" << qtyToString(qty_)
                << " price:" << priceToString(price_)
                << " priority:" << priorityToString(priority_)
                << "]";
            return ss.str();
        }
    };


#pragma pack(pop)
using MEMarketUpdateLFQueue = LFQueue<MEMarketUpdate>;
}