#pragma once
#include "common/types.h"
#include "common/constants.h"
#include "common/logging.h"
#include "common/time-utils.h"
#include "common/thread-utils.h"
#include "common/lf-queue.h"
#include "matching-engine/market-data/market-update.h"
#include "matching-engine/order-server/client-request.h"
#include "matching-engine/order-server/client-response.h"
#include "matching-engine/matcher/me-order.h"
#include "matching-engine/order-book/order-book.h"

using namespace Common;

namespace Exchange{
    class MatchingEngine final{
        public:
        MatchingEngine(
            ClientRequestLFQueue* client_requests,
            ClientResponseLFQueue* client_responses,
            MEMarketUpdateLFQueue* market_updates);
        ~MatchingEngine();
        auto start() -> void;
        auto stop() -> void;
        // Delete defaults.
        MatchingEngine() = delete;
        MatchingEngine(const MatchingEngine &) = delete;
        MatchingEngine(const MatchingEngine &&) = delete;
        MatchingEngine &operator=(const MatchingEngine &) = delete;
        MatchingEngine &operator=(const MatchingEngine &&) = delete;
        void run() noexcept;
        void processClientRequest(const MEClientRequest* incoming_request) const noexcept;
        void sendClientResponse(const MEClientResponse *client_response) noexcept;
        void sendMarketUpdate(const MEMarketUpdate *market_update) noexcept;
    
        private:
        //ToDo
        // Alignment
        ClientRequestLFQueue* incoming_requests_ = nullptr;
        ClientResponseLFQueue* outgoing_responses_ = nullptr;
        MEMarketUpdateLFQueue* outgoing_market_updates_ = nullptr;
        volatile bool run_ = false;
        Logger logger_;
        std::string time_str_;
        std::array<MEOrderBook,ME_MAX_TICKERS> ticker_order_book_;
        
    };


}