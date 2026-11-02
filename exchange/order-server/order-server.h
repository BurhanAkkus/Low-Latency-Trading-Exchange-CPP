#pragma once
#include "common/constants.h"
#include "common/types.h"
#include "common/lf-queue.h"
#include "common/macros.h"
// #include "common/tcp-server.h"
#include "exchange/order-server/shm-server.h"
#include "exchange/order-server/client-request.h"
#include "exchange/order-server/client-response.h"
#include "exchange/order-server/fifo-sequencer.h"
#include <array>
using namespace Common;

namespace Exchange{
    class OrderServer{
        public:
        // Shared memory: no address to listen on.
        // OrderServer(ClientRequestLFQueue *client_requests,
        //     ClientResponseLFQueue *client_responses, const std::string &iface,
        //     int port);
        OrderServer(ClientRequestLFQueue *client_requests, ClientResponseLFQueue *client_responses);
        ~OrderServer();
        // core_id -1 leaves the thread unpinned.
        auto start(int core_id = -1) noexcept -> void;
        auto stop() noexcept -> void;
        private:
        std::atomic<bool> run_;
        std::thread* thread_ = nullptr;
        // const std::string iface_;
        // const int port_ = 0;;
        ClientRequestLFQueue* client_requests_{nullptr};
        ClientResponseLFQueue* client_responses_{nullptr};
        std::string time_str_;
        Logger logger_;
        std::array<size_t, ME_MAX_NUM_CLIENTS> cid_next_outgoing_seq_num_;
        std::array<size_t, ME_MAX_NUM_CLIENTS> cid_next_expected_seq_num_;
        // Shared memory in place of TCP (the TCP version is on main).
        // std::array<TCPSocket*,ME_MAX_NUM_CLIENTS> client_to_port_;
        // TCPServer tcp_server_;
        std::array<ShmSocket*,ME_MAX_NUM_CLIENTS> client_to_port_;
        ShmServer server_;
        FIFOSequencer fifo_sequencer_;

        // rx_time is TCP's kernel receive time, unused: each request carries its client's send_tsc_.
        auto recvCallback(ShmSocket *socket, Nanos) noexcept {
            // logger_.log("%:% %() % Received socket:% len:% rx:%\n", __FILE__,
            // __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
            //             socket->fd_, socket->next_rcv_valid_index_, rx_time);
            
            // There is a complete object.
            // ToDo - performance
            // Skip. we only read whole objects.
            if (socket->next_rcv_valid_index_ >= sizeof(OMClientRequest)) {
                size_t i = 0;
                for (; i + sizeof(OMClientRequest) <= socket->next_rcv_valid_index_; i += sizeof(OMClientRequest)) {
                    // request points to the socket's data.
                    auto request = reinterpret_cast<const OMClientRequest*>(socket->rcv_buffer_ + i);
                    // Check if this is the first request from this client.
                    if (UNLIKELY(client_to_port_[request->me_client_request_.client_id_]
                        == nullptr)) {
                            client_to_port_[request->me_client_request_.client_id_] = socket;
                            // The client's socket: T6 is logged per socket send, by fd.
                            TRACE(T6fd, (request->me_client_request_.client_id_ << 32) | static_cast<uint64_t>(socket->fd_), logger_);
                        }
                    // Client is connected to another socket.
                    // ToDo
                    // Turn this into assert.
                    else if(client_to_port_[request->me_client_request_.client_id_]  != socket){
                            continue;
                    }
                    // Wrong sequence number. skip.
                    auto &next_expected_seq_num = cid_next_expected_seq_num_[request->me_client_request_.client_id_];
                    if (request->seq_num_ != next_expected_seq_num) {
                        continue;
                        //ToDo - correctness
                        // send rejected.
                    }
                    next_expected_seq_num++;
                    // Read from the client's ring.
                    TRACE(T1, traceKey(request->me_client_request_), logger_);
                    // Sequenced by when the client sent it, on the TSC every process shares.
                    // fifo_sequencer_.addClientRequest(rx_time, request->me_client_request_,i);
                    // ToDo - performance
                    // carry them all together at once.
                    fifo_sequencer_.addClientRequest(static_cast<Nanos>(request->send_tsc_), request->me_client_request_,i);
                };
                // copy the last incomplete portion to the beginning.
                // we have read the portion [0,i].
                memcpy(socket->rcv_buffer_, socket->rcv_buffer_ + i, socket->next_rcv_valid_index_ - i);
                socket->next_rcv_valid_index_ -= i;
            }
        }
        auto recvFinishedCallback() noexcept {
            fifo_sequencer_.publishReceived();
        }
        auto run() noexcept -> void;
        auto sendClientResponses() noexcept -> void;
    };
}