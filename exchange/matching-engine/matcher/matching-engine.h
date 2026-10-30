#pragma once
#include "common/types.h"
#include "common/constants.h"
#include "common/logging.h"
#include "common/time-utils.h"
#include "common/perf-utils.h"
#include "common/thread-utils.h"
#include "common/lf-queue.h"
#include "exchange/market-data/market-update.h"
#include "exchange/order-server/client-request.h"
#include "exchange/order-server/client-response.h"
#include "exchange/matching-engine/matcher/me-order.h"
#include "exchange/matching-engine/order-book/order-book.h"

using namespace Common;

namespace Exchange{
    class MatchingEngine final{
        public:
        // logger_core_id / core_id pin the logger / matching engine thread, -1 leaves it to the OS.
        MatchingEngine(
            ClientRequestLFQueue* client_requests,
            ClientResponseLFQueue* client_responses,
            MEMarketUpdateLFQueue* market_updates,
            int logger_core_id = -1);
        ~MatchingEngine();
        auto start(int core_id = -1) -> void;
        auto stop() -> void;
        // Delete defaults.
        MatchingEngine() = delete;
        MatchingEngine(const MatchingEngine &) = delete;
        MatchingEngine(const MatchingEngine &&) = delete;
        MatchingEngine &operator=(const MatchingEngine &) = delete;
        MatchingEngine &operator=(const MatchingEngine &&) = delete;
        void run() noexcept;
        void processClientRequest(const MEClientRequest* incoming_request) noexcept;
        void inline sendClientResponse(const MEClientResponse *client_response) noexcept;
        void inline sendMarketUpdate(const MEMarketUpdate *market_update) noexcept;
    
        private:
        //ToDo
        // Alignment
        ClientRequestLFQueue* incoming_requests_ = nullptr;
        ClientResponseLFQueue* outgoing_responses_ = nullptr;
        MEMarketUpdateLFQueue* outgoing_market_updates_ = nullptr;
        std::atomic<bool> run_{false};
        Logger logger_;
        std::string time_str_;
        std::thread* thread_ = nullptr;
        std::array<MEOrderBook*,ME_MAX_TICKERS> ticker_order_book_;
    };

    // Defined in the header so the order book can inline them.
    void inline MatchingEngine::sendClientResponse(const MEClientResponse *client_response) noexcept {
        // logger_.log("%:% %() % Sending %\n", __FILE__, __LINE__,
        //     __FUNCTION__, Common::getCurrentNanos(),
        //     client_response->toString());
        // A/B experiment 9: every response published on its own, as before 6aa4733.
        *outgoing_responses_->getNextWriteTo() = *client_response;
        outgoing_responses_->updateWriteIndex();
        // TTT_MEASURE(T4t_MatchingEngine_LFQueue_write, logger_);
    }
    void inline MatchingEngine::sendMarketUpdate(const MEMarketUpdate *market_update) noexcept {
        // logger_.log("%:% %() % Sending %\n", __FILE__, __LINE__,
        //     __FUNCTION__, Common::getCurrentNanos(),
        //     market_update->toString());
        // A/B experiment 9: every market update published on its own, as before 6aa4733.
        *outgoing_market_updates_->getNextWriteTo() = *market_update;
        outgoing_market_updates_->updateWriteIndex();
        // TTT_MEASURE(T4_MatchingEngine_LFQueue_write, logger_);
    }

}