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
        OrderServer(ClientRequestLFQueue *client_requests,
            ClientResponseLFQueue *client_responses, const std::string &iface,
            int port);
        ~OrderServer();
        auto start() noexcept -> void;
        auto stop() noexcept -> void;
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
        std::array<TCPSocket*,ME_MAX_NUM_CLIENTS> client_to_port_;
        TCPServer tcp_server_;
        FIFOSequencer fifo_sequencer_;
        auto recvCallback(TCPSocket *socket, Nanos rx_time) noexcept {
            // logger_.log("%:% %() % Received socket:% len:% rx:%\n", __FILE__,
            // __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
            //             socket->fd_, socket->next_rcv_valid_index_, rx_time);
            
            // There is a complete object.
            if (socket->next_rcv_valid_index_ >= sizeof(OMClientRequest)) {
                size_t i = 0;
                for (; i + sizeof(OMClientRequest) <= socket->next_rcv_valid_index_; i += sizeof(OMClientRequest)) {
                    // request points to the socket's data.
                    auto request = reinterpret_cast<const OMClientRequest*>(socket->rcv_buffer_ + i);
                    // Check if this is the first request from this client.
                    if (UNLIKELY(client_to_port_[request->me_client_request_.client_id_] 
                        == nullptr)) {
                            client_to_port_[request->me_client_request_.client_id_] = socket;
                        }
                    // Client is connected to another socket.
                    else if(client_to_port_[request->me_client_request_.client_id_]  != socket){
                        logger_.log("%:% %() % Received ClientRequest from ClientId:% on different socket:% expected:%\n",
                             __FILE__, __LINE__, __FUNCTION__,
                            Common::getCurrentTimeStr(&time_str_),
                            request->me_client_request_.client_id_,
                            socket->fd_,
                            client_to_port_[request->me_client_request_.client_id_]->fd_);
                            continue;
                    }
                    // Wrong sequence number. skip.
                    auto &next_expected_seq_num = cid_next_expected_seq_num_[request->me_client_request_.client_id_];
                    if (request->seq_num_ != next_expected_seq_num) {
                        logger_.log("%:% %() % Incorrect sequence number. ClientId:% SeqNum expected:% received:%\n",
                            __FILE__,__LINE__, __FUNCTION__,
                            Common::getCurrentTimeStr(&time_str_),
                            request->me_client_request_.client_id_,
                            next_expected_seq_num, request->seq_num_);
                        continue;
                        //ToDo - correctness
                        // send rejected.
                    }
                    next_expected_seq_num++;
                    fifo_sequencer_.addClientRequest(rx_time, request->me_client_request_);
                };
                memcpy(socket->rcv_buffer_, socket->rcv_buffer_ + i, socket->next_rcv_valid_index_ - i);
                socket->next_rcv_valid_index_ -= i;
            }
        }
    }
}