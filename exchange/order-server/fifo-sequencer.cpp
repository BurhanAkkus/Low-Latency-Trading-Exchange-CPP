#include "exchange/order-server/fifo-sequencer.h"

using namespace Common;

namespace Exchange{
    void FIFOSequencer::publishOutputs() noexcept{
            incoming_requests_->updateWriteIndex(pending_client_requests_);
            pending_client_requests_ = 0;
        }
    void FIFOSequencer::publishReceived() noexcept{
        // ToDo - research
        // faster sort for this instance? 
        // radix sort, we can use the previous publish's timestamp
        // and the current time so we have both an upper and a lower bound for values.
        std::sort(poll_results_.begin(),poll_results_.begin() + poll_results_size_);
        // ToDo - improvement
        // copy memory instead of copying 1 by 1.
        for(auto& client_request:poll_results_){
            *incoming_requests_->getNextWriteTo(pending_client_requests_++) = client_request.request_;
        }
        publishOutputs();
    }
}