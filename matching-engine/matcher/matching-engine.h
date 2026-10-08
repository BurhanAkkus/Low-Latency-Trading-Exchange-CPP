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
        MatchingEngine &operator=(const MatchingEngine &) =
        delete;
        MatchingEngine &operator=(const MatchingEngine &&) =
        delete;
        private:
        OrderBookHashMap ticker_order_book_;
        ClientRequestLFQueue* incoming_requests_ = nullptr;
        ClientResponseLFQueue* outgoing_responses_ = nullptr;
        MEMarketUpdateLFQueue* outgoing_market_updates_ = nullptr;
        volatile bool run_ = false;
        std::string time_str_;
        Logger logger_;

        auto run() noexcept {
            logger_.log("%:% %() %\n", __FILE__, __LINE__,
                __FUNCTION__, Common::getCurrentTimeStr(&time_str_));
            while (run_) {
                const auto me_client_request =
                    incoming_requests_->getNextReadTo();
                if (LIKELY(me_client_request)) {
                logger_.log("%:% %() % Processing %\n", __FILE__,
                        __LINE__, __FUNCTION__,
                        Common::getCurrentTimeStr(&time_str_),
                        me_client_request->toString());
                processClientRequest(me_client_request);
                incoming_requests_->updateReadIndex();
                }
            }
        }

        auto processClientRequest(const MEClientRequest* incoming_request) const noexcept -> void{
            auto order_book = ticker_order_book_[incoming_request->tickerId_];
            switch(incoming_request->type_){
                case ClientRequestType::NEW:
                    order_book -> add(incoming_request);
                    return;
                case ClientRequestType::CANCEL:
                    order_book -> cancel(incoming_request);
                    return;
                default:
                    FATAL("Received INVALID client request!!");
                return;
            }
        }

        auto sendClientResponse(const MEClientResponse *client_response) noexcept {
            logger_.log("%:% %() % Sending %\n", __FILE__, __LINE__,
                __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
                client_response->toString());
            auto next_write = outgoing_responses_->getNextWriteTo();
            *next_write = std::move(*client_response);
            outgoing_responses_->updateWriteIndex();
        }   
        auto sendMarketUpdate(const MEMarketUpdate *market_update) noexcept {
            logger_.log("%:% %() % Sending %\n", __FILE__, __LINE__,
                __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
                market_update->toString());
            auto next_write = outgoing_market_updates_->getNextWriteTo();
            *next_write = *market_update;
            outgoing_market_updates_->updateWriteIndex();
        }

    };


}