#pragma once
#include "common/constants.h"
#include "common/macros.h"
#include "common/thread-utils.h"
#include "common/time-utils.h"
#include "exchange/order-server/client-request.h"
#include <functional>
//#include "common/logging.h"
using namespace Common;

namespace Exchange{
    // ToDo - improvement
    // we don't need this at all.
    // all it does is sort the poll results and publish to client requests q.
    // can be inlined.
    class FIFOSequencer{
        public:
        FIFOSequencer(ClientRequestLFQueue *client_requests, std::function<void()> while_full)//, Logger *logger)
            : incoming_requests_(client_requests), while_full_(std::move(while_full))//, logger_(logger
            {}
        // ToDo - improvement
        // we lose sort from each socket.
        // maybe keep a different array for each socket and use merge only.
        // Another idea, insert like a heap to assist with the sort.
        auto addClientRequest(Nanos rx_time, const MEClientRequest &request, size_t order_within_client) {
            if (UNLIKELY(poll_results_size_ >= poll_results_.size())) {
                publishReceived();
            }
            poll_results_.at(poll_results_size_++) = std::move(RecvTimeClientRequest{rx_time, order_within_client,request});
        }

        void publishReceived() noexcept;
        private:
        struct RecvTimeClientRequest {
            Nanos recv_time_ = 0;
            size_t order_within_client{0};
            MEClientRequest request_;
            bool operator<(const RecvTimeClientRequest &rhs) const noexcept{
                return (recv_time_ == rhs.recv_time_ ? 
                    (order_within_client < rhs.order_within_client) : 
                    (recv_time_ < rhs.recv_time_));
            }
        };
        ClientRequestLFQueue* incoming_requests_{nullptr};
        // Called while the request queue is full. The engine frees request slots only while it
        // can write its responses, so the order server keeps reading them here, otherwise both
        // threads wait for each other forever (lf-queue.h caveat 2).
        std::function<void()> while_full_;
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