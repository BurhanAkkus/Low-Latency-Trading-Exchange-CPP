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
    
}