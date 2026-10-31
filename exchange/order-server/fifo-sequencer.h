#pragma once
#include "common/constants.h"
#include "common/macros.h"
#include "common/thread-utils.h"
#include "common/time-utils.h"
#include "exchange/order-server/client-request.h"
//#include "common/logging.h"
using namespace Common;

namespace Exchange{
    // ToDo - improvement
    // we don't need this at all.
    // all it does is sort the poll results and publish to client requests q.
    // can be inlined.
    class FIFOSequencer{
        public:
        FIFOSequencer(ClientRequestLFQueue *client_requests)//, Logger *logger)
            : incoming_requests_(client_requests)//, logger_(logger
            {}
        // ToDo - improvement
        // we lose sort from each socket.
        // maybe keep a different array for each socket and use merge only.
        // Another idea, insert like a heap to assist with the sort.
        auto addClientRequest(Nanos rx_time, const MEClientRequest &request) {
            if (UNLIKELY(poll_results_size_ >= poll_results_.size())) {
                FATAL("Too many pending requests");
            }
            poll_results_.at(poll_results_size_++) = std::move(RecvTimeClientRequest{rx_time, request});
        }

        void publishReceived() noexcept;
        private:
        struct RecvTimeClientRequest {
            Nanos recv_time_ = 0;
            MEClientRequest request_;
            bool operator<(const RecvTimeClientRequest &rhs) const noexcept{
                return (recv_time_ < rhs.recv_time_);
            }
        };
        ClientRequestLFQueue* incoming_requests_{nullptr};
        std::string time_str_;
        //Logger* logger_{nullptr};
        // ToDo - improvement
        // sort only the times from the poll results.
        // Instantiate the objects directly in the client_requests queue instead of here.
        std::array<RecvTimeClientRequest, ME_MAX_PENDING_REQUESTS> poll_results_;
        size_t poll_results_size_{0};
        void publishOutputs() noexcept;
    };
}