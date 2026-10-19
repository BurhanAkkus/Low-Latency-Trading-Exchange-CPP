#include "exchange/order-server/fifo-sequencer.h"

using namespace Common;

namespace Exchange{
    void FIFOSequencer::publishOutputs() noexcept{
            incoming_requests_->updateWriteIndex(pending_client_requests_);
            pending_client_requests_ = 0;
        }
    void FIFOSequencer::publishReceived() const noexcept{
        // ToDo - research
        // faster sort for this instance? 
        // radix sort maybe, we already know the previous publish's last value
        // and the current time so we have both an upper and a lower bound for values.
        std::sort(poll_results_.begin(),poll_results_.begin() + poll_results_size_);
        incoming_requests_.
    }
}