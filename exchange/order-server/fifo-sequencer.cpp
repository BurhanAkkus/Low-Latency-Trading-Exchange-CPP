#include "exchange/order-server/fifo-sequencer.h"

using namespace Common;

namespace Exchange{
    void FIFOSequencer::publishOutputs() noexcept{
            incoming_requests_->updateWriteIndex(poll_results_size_);
            poll_results_size_ = 0;
        }
    void FIFOSequencer::publishReceived() noexcept{
        // ToDo - research
        // faster sort for this instance? 
        // radix sort, we can use the previous publish's timestamp
        // and the current time so we have both an upper and a lower bound for values.
        std::sort(poll_results_.begin(),poll_results_.begin() + poll_results_size_);
        // ToDo - improvement
        // copy memory instead of copying 1 by 1.
        for(size_t i = 0; i < poll_results_size_; i++){
            while(UNLIKELY(!incoming_requests_->hasSpace(i + 1))){
                while_full_();
                __builtin_ia32_pause();
            }
            *incoming_requests_->getNextWriteTo(i) = poll_results_[i].request_;
        }
        publishOutputs();
    }
}