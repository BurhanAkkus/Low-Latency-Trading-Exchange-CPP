#pragma once
#include "common/constants.h"
#include "common/types.h"
#include "common/lf-queue.h"
#include "common/macros.h"
// #include "common/tcp-server.h"
#include "exchange/order-server/shm-server.h"
#include "exchange/order-server/client-request.h"
#include "exchange/order-server/client-response.h"
// #include "exchange/order-server/fifo-sequencer.h"
#include <algorithm>
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
        // Requests go straight into the request queue and are sorted there 
        // This pass's requests, in the request queue's slots after the published ones: batch_ is
        // the first, pending_ how many. recvFinishedCallback() sorts and publishes them.
        ClientRequestLFQueue::Unpublished batch_;
        size_t pending_ = 0;
        // Slots after these known to be free, counted down per request: at 0 the queue is asked
        // again.
        size_t free_slots_ = 0;

        // One request, read from the socket's ring. False if the request queue has no room for
        // it: it stays in the ring. No rx_time as with TCP: each request carries its client's
        // send_tsc_.
        auto recvCallback(ShmSocket *socket, const OMClientRequest& request) noexcept -> bool {
            // logger_.log("%:% %() % Received socket:% len:% rx:%\n", __FILE__,
            // __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
            //             socket->fd_, socket->next_rcv_valid_index_, rx_time);

            // Check if this is the first request from this client.
            if (UNLIKELY(client_to_port_[request.me_client_request_.client_id_]
                == nullptr)) {
                    client_to_port_[request.me_client_request_.client_id_] = socket;
                    // The client's socket: T6 is logged per socket send, by fd.
                    TRACE(T6fd, (request.me_client_request_.client_id_ << 32) | static_cast<uint64_t>(socket->fd_), logger_);
                }
            // Client is connected to another socket.
            // ToDo
            // Turn this into assert.
            else if(client_to_port_[request.me_client_request_.client_id_]  != socket){
                    return true;
            }
            // Wrong sequence number. skip.
            auto &next_expected_seq_num = cid_next_expected_seq_num_[request.me_client_request_.client_id_];
            if (request.seq_num_ != next_expected_seq_num) {
                return true;
                //ToDo - correctness
                // send rejected.
            }
            if (UNLIKELY(!free_slots_)) {
                // Reloads the engine's read index only if the queue's cached one says it's full.
                free_slots_ = client_requests_->freeSlots(pending_ + 1) - pending_;
                // Before its seq number is counted: the next pass reads it again.
                if (!free_slots_) return false;
            }
            --free_slots_;
            next_expected_seq_num++;
            // Read from the client's ring.
            TRACE(T1, traceKey(request.me_client_request_), logger_);
            if (!pending_) batch_ = client_requests_->unpublished();
            batch_[pending_++] = request.me_client_request_;
            return true;
        }
        // Sorts this pass's requests by when their clients sent them, on the TSC every process
        // shares, and publishes them.
        auto recvFinishedCallback() noexcept {
            if (!pending_) return;
            // ToDo - research
            // faster sort for this instance?
            // radix sort, we can use the previous publish's timestamp
            // and the current time so we have both an upper and a lower bound for values.
            // ToDo - improvement
            // each client's requests are already in send order: merge them instead of sorting.
            const auto end = batch_ + static_cast<std::ptrdiff_t>(pending_);
            std::sort(batch_, end, [](const MEClientRequest& a, const MEClientRequest& b) { return a.send_tsc_ < b.send_tsc_; });
            client_requests_->updateWriteIndex(pending_);
            // No T2: it read the just-written slots back for their keys and logged a line per
            // request. T1 -> T3 covers the sort, the publish and the wait in the queue.
            // In the request queue, the engine can read them from here on.
            // for (auto it = batch_; it != end; ++it) TRACE(T2, traceKey(*it), logger_);
            pending_ = 0;
        }
        auto run() noexcept -> void;
        auto sendClientResponses() noexcept -> void;
    };
}