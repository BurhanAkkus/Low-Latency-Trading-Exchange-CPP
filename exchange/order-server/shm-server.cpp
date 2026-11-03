#include "exchange/order-server/shm-server.h"
#include "common/perf-utils.h"
#include "common/time-utils.h"
#include <algorithm>

namespace Exchange{
    auto ShmSocket::send(const void* data, size_t len) noexcept -> bool {
        if (UNLIKELY(send_disconnected_)) return false;
        const auto send_start = Common::rdtsc();
        const auto sent = ResponseRing::write(channels_->response_counters_[fd_], channels_->response_buffers_[fd_],
            data, len);
        // The client doesn't read what it's sent: drop it rather than lose data or stall everyone.
        // A part that fit stays unread, the client reads whole responses only.
        if (UNLIKELY(sent != len)) {
            send_disconnected_ = true;
            return false;
        }
        // In the client's ring. As for TCP, by fd: the analysis takes the first write after a
        // response's T5, S0 is when it started.
        TRACE_AT(S0, fd_, send_start, logger_);
        TRACE(T6, fd_, logger_);
        return true;
    }

    auto ShmSocket::receive(size_t read, size_t written) noexcept -> bool {
        const auto& buffer = channels_->request_buffers_[fd_];
        // One request at a time, copied out whole: it can wrap around the ring's end. Its ring
        // position orders the client's own requests.
        OMClientRequest request;
        auto next = read;
        while (RequestRing::read(buffer, next, written, &request, sizeof(request))) {
            recv_callback_(this, request, next);
            next += sizeof(request);
        }
        // Not expected: the pass lists a client only with a whole request in its ring.
        if (next == read) return false;
        channels_->request_counters_[fd_].next_to_read_.store(next, std::memory_order_release);
        return true;
    }

    ShmServer::~ShmServer() {
        for (auto socket : sockets_) delete socket;
    }

    auto ShmServer::listen() -> void {
        channels_ = std::make_unique<SharedRegion<OrderChannels>>(ORDER_CHANNELS_NAME, SharedRegion<OrderChannels>::Open::CREATE);
        logger_.log("%:% %() % shared memory % ready for % clients\n", __FILE__, __LINE__, __FUNCTION__,
            Common::getCurrentTimeStr(&time_str_), ORDER_CHANNELS_NAME, ME_MAX_NUM_CLIENTS);
    }

    auto ShmServer::poll() noexcept -> void {
        for (auto socket : disconnected_sockets_) {
            std::erase(sockets_, socket);
            by_slot_[socket->fd_] = nullptr;
            disconnect_callback_(socket);
            // The slot isn't reused in this run: it stays CLOSED with its rings as they are, a new
            // client there would read the old one's leftover responses. Dropped by us, the client
            // sees CLOSED too.
            channels_->get()->states_[socket->fd_].state_.store(ClientState::CLOSED, std::memory_order_release);
            logger_.log("%:% %() % dropped client slot:% closed by the client:%\n", __FILE__, __LINE__, __FUNCTION__,
                Common::getCurrentTimeStr(&time_str_), socket->fd_, socket->closed_by_client_);
            delete socket;
        }
        disconnected_sockets_.clear();
        // Clients count every CONNECTED and CLOSED: nothing to rescan until it moves.
        const auto changes = channels_->get()->changes_.load(std::memory_order_acquire);
        if (LIKELY(changes == seen_changes_)) return;
        seen_changes_ = changes;
        auto& states = channels_->get()->states_;
        for (size_t slot = 0; slot < states.size(); ++slot) {
            const auto state = states[slot].state_.load(std::memory_order_acquire);
            auto socket = by_slot_[slot];
            // ToDo - performance
            // early exit.
            if (state == ClientState::CONNECTED && !socket) {
                // Allocated like a TCP accept.
                socket = new ShmSocket(logger_);
                socket->fd_ = static_cast<int>(slot);
                socket->channels_ = channels_->get();
                socket->recv_callback_ = recv_callback_;
                by_slot_[slot] = socket;
                sockets_.push_back(socket);
                logger_.log("%:% %() % client connected slot:%\n", __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str_), slot);
            } else if (state == ClientState::CLOSED && socket && !socket->closed_by_client_) {
                socket->closed_by_client_ = true;
                markDisconnected(socket);
            }
        }
    }

    auto ShmServer::sendAndRecv() noexcept -> void {
        // When this pass started: P1 for the analysis, logged only for passes that read something.
        const auto pass_start = Common::rdtsc();
        // Phase 1: which clients have a whole request to read. Only the request counters, back to
        // back: a client with nothing new costs two loads, its socket isn't touched.
        const auto& counters = channels_->get()->request_counters_;
        size_t to_read = 0;
        for (size_t slot = 0; slot < counters.size(); ++slot) {
            const auto written = counters[slot].next_to_write_.load(std::memory_order_acquire);
            // Only we store it.
            const auto read = counters[slot].next_to_read_.load(std::memory_order_relaxed);
            if (written - read >= sizeof(OMClientRequest)) to_read_[to_read++] = {slot, read, written};
        }
        // Phase 2: read them, up to what phase 1 saw. A slot without a socket isn't picked up by
        // poll() yet, or was dropped.
        size_t received = 0;
        for (size_t i = 0; i < to_read; ++i) {
            const auto& [slot, read, written] = to_read_[i];
            if (auto socket = by_slot_[slot]; socket && socket->receive(read, written)) ++received;
        }
        if (received) {
            TRACE_AT(P1, received, pass_start, logger_);
            recv_finished_callback_();
        }
    }

    // The next poll() drops it.
    auto ShmServer::markDisconnected(ShmSocket* socket) noexcept -> void {
        // ToDo - modernization
        // use c++20 find
        if (std::find(disconnected_sockets_.begin(), disconnected_sockets_.end(), socket) == disconnected_sockets_.end())
            disconnected_sockets_.push_back(socket);
    }
}
