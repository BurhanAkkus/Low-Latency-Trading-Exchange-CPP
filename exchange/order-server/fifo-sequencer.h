#pragma once
#include "common/constants.h"
#include "common/macros.h"
#include "common/thread-utils.h"
#include "common/time-utils.h"
#include "exchange/order-server/client-request.h"
#include "common/logging.h"
using namespace Common;

namespace Exchange{
    class FIFOSequencer{
        struct RecvTimeClientRequest {
            Nanos recv_time_ = 0;
            MEClientRequest request_;
            bool operator<(const RecvTimeClientRequest &rhs) const noexcept{
                return (recv_time_ < rhs.recv_time_);
            }
        };
        public:
            FIFOSequencer(ClientRequestLFQueue *client_requests, Logger *logger)
                : incoming_requests_(client_requests), logger_(logger) {
            }
        private:
        ClientRequestLFQueue* incoming_requests_{nullptr};
        std::string time_str_;
        Logger* logger_{nullptr};
        //ToDo - improvement
        // sort only the times from the poll results.
        // Instantiate the objects directly in the client_requests queue instead of here.
        std::array<RecvTimeClientRequest, ME_MAX_NUM_CLIENTS> poll_results_;
        size_t poll_results_size_{0};
        size_t pending_client_requests_{0};
        void publishReceived() const noexcept;
        void publishOutputs() noexcept;
    };
}