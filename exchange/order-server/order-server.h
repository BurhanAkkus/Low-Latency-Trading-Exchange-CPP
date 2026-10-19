#pragma once
#include "common/constants.h"
#include "common/types.h"
#include "common/lf-queue.h"
#include "common/macros.h"
#include "common/tcp-server.h"
#include "exchange/order-server/client-request.h"
#include "exchange/order-server/client-response.h"
#include "exchange/order-server/fifo-sequencer.h"
#include <array>
using namespace Common;

namespace Exchange{
    class OrderServer{
        private:
        volatile bool run_;    
        const std::string iface_;
        const int port_ = 0;;
        ClientRequestLFQueue* client_requests_{nullptr};
        ClientResponseLFQueue* client_responses_{nullptr};
        std::string time_str_;
        Logger logger_;
        std::array<size_t, ME_MAX_NUM_CLIENTS> cid_next_outgoing_seq_num_;
        std::array<size_t, ME_MAX_NUM_CLIENTS> cid_next_expected_seq_num_;
        std::array<uint32_t,ME_MAX_NUM_CLIENTS> client_to_port_;
        TCPServer tcp_server_;
        FIFOSequencer fifo_sequencer_;
    };
}