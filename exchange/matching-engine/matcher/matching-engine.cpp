#include "matching-engine.h"

using namespace Common;

namespace Exchange{
    MatchingEngine::MatchingEngine(ClientRequestLFQueue* client_requests,
            ClientResponseLFQueue* client_responses,
            MEMarketUpdateLFQueue* market_updates,
            int logger_core_id):
            incoming_requests_{client_requests},
            outgoing_responses_{client_responses},
            outgoing_market_updates_{market_updates},
            logger_{"exchange_matching_engine.log", logger_core_id}{
                //ToDo
                // should initialize in consecutive memory chunk.
                 for(uint64_t i = 0; i < ticker_order_book_.size(); i++){
                    ticker_order_book_[i] = new MEOrderBook{this,i,&logger_};
                 }
            };
    MatchingEngine::~MatchingEngine(){
        run_ = false;
        if (thread_) { thread_->join(); delete thread_; thread_ = nullptr;}
        for(auto& order_book : ticker_order_book_) {
            delete order_book;
            order_book = nullptr;
        }
        incoming_requests_ = nullptr;
        outgoing_responses_ = nullptr;
        outgoing_market_updates_ = nullptr;
    }
    auto MatchingEngine::start(int core_id) -> void {
        run_ = true;
        thread_ = Common::createAndStartThread(core_id, "Exchange/MatchingEngine", [this]() { run(); });
        ASSERT(thread_ != nullptr, "Failed to start MatchingEngine thread.");
    }
    auto MatchingEngine::stop() -> void {
        run_ = false;
    }
    void MatchingEngine::run() noexcept {
            logger_.log("%:% %() %\n", __FILE__, __LINE__,
                __FUNCTION__, Common::getCurrentTimeStr(&time_str_));
            while (true) {
                // Read run_ before the queue: once it reads false, an empty queue means every
                // request queued before stop() has been processed.
                const bool stopping = !run_;
                const auto me_client_request =
                    incoming_requests_->getNextReadTo();
                if (LIKELY(me_client_request)) {
                // TTT_MEASURE(T3_MatchingEngine_LFQueue_read, logger_);
                // logger_.log("%:% %() % Processing %\n", __FILE__,
                //         __LINE__, __FUNCTION__,
                //         Common::getCurrentNanos(),
                //         me_client_request->toString());
                START_MEASURE(ME_req);
                processClientRequest(me_client_request);
                publishOutputs();
                END_MEASURE(ME_req, logger_);
                incoming_requests_->updateReadIndex();
                } else if (stopping) {
                    return;
                }
            }
        }

    void inline MatchingEngine::publishOutputs() noexcept{
        outgoing_responses_->updateWriteIndex(pending_client_responses_);
        pending_client_responses_ = 0;
        outgoing_market_updates_->updateWriteIndex(pending_market_updates_);
        pending_market_updates_ = 0;
    }

    auto MatchingEngine::processClientRequest(const MEClientRequest* incoming_request) noexcept -> void{
        auto& order_book = ticker_order_book_[incoming_request->ticker_id_];
        switch(incoming_request->type_){
            case ClientRequestType::NEW: {
                // START_MEASURE(Exchange_MEOrderBook_add);
                order_book->add(incoming_request->client_id_,incoming_request->order_id_,
                    incoming_request->side_,incoming_request->price_,incoming_request->qty_);
                // END_MEASURE(Exchange_MEOrderBook_add, logger_);
                return;
            }
            case ClientRequestType::CANCEL: {
                // START_MEASURE(Exchange_MEOrderBook_cancel);
                order_book->cancel(incoming_request->client_id_,incoming_request->order_id_);
                // END_MEASURE(Exchange_MEOrderBook_cancel, logger_);
                return;
            }
            default:
                FATAL("Received INVALID client request!!");
            return;
        }
    }

}