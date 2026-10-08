#include "matching-engine.h"

using namespace Common;

namespace Exchange{
    MatchingEngine::MatchingEngine(ClientRequestLFQueue* client_requests,
            ClientResponseLFQueue* client_responses,
            MEMarketUpdateLFQueue* market_updates):
            incoming_requests_{client_requests},
            outgoing_responses_{client_responses},
            outgoing_market_updates_{market_updates},
            logger_{"exchange_matching_engine.log"}{
                // for(auto i = 0; i < ticker_order_book_.size(); i++){
                //     ticker_order_book_[i] = new MEOrderBook(i,&logger_,this);
                // }
            };
    MatchingEngine::~MatchingEngine(){
        using namespace std::literals::chrono_literals;
        std::this_thread::sleep_for(1s);
        // for(auto& order_book : ticker_order_book_) {
        //     delete order_book;
        //     order_book = nullptr;
        // }
        run_ = false;
        incoming_requests_ = nullptr;
        outgoing_responses_ = nullptr;
        outgoing_market_updates_ = nullptr;
    }
    auto MatchingEngine::start() -> void {
        run_ = true;
        ASSERT(Common::createAndStartThread(-1,
        "Exchange/MatchingEngine", [this]() { run(); }) !=
        nullptr, "Failed to start MatchingEngine thread.");
    }
    auto MatchingEngine::stop() -> void {
        run_ = false;
    }
    void MatchingEngine::run() noexcept {
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

    auto MatchingEngine::processClientRequest(const MEClientRequest* incoming_request) const noexcept -> void{
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

    auto MatchingEngine::sendClientResponse(const MEClientResponse *client_response) noexcept {
        logger_.log("%:% %() % Sending %\n", __FILE__, __LINE__,
            __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
            client_response->toString());
        auto next_write = outgoing_responses_->getNextWriteTo();
        *next_write = std::move(*client_response);
        outgoing_responses_->updateWriteIndex();
    }   
    auto MatchingEngine::sendMarketUpdate(const MEMarketUpdate *market_update) noexcept {
        logger_.log("%:% %() % Sending %\n", __FILE__, __LINE__,
            __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
            market_update->toString());
        auto next_write = outgoing_market_updates_->getNextWriteTo();
        *next_write = *market_update;
        outgoing_market_updates_->updateWriteIndex();
    }

}