#include "exchange/order-server/shm-server.h"
#include "common/perf-utils.h"
#include "common/time-utils.h"
#include <algorithm>

namespace Exchange{
    auto ShmSocket::send(const void* data, size_t len) noexcept -> void {
        // The client doesn't read what it's sent: drop it rather than lose data or stall everyone.
        if (UNLIKELY(send_disconnected_ || next_send_valid_index_ + len > SEND_BUFFER_SIZE)) {
            send_disconnected_ = true;
            return;
        }
        memcpy(send_buffer_ + next_send_valid_index_, data, len);
        next_send_valid_index_ += len;
    }

    auto ShmSocket::receive(size_t read, size_t written) noexcept -> bool {
        const auto n = RequestRing::read(channels_->request_buffers_[fd_], read, written,
            rcv_buffer_ + next_rcv_valid_index_, RECEIVE_BUFFER_SIZE - next_rcv_valid_index_);
        // Not expected: the pass lists a client only with a whole request in its ring, and the
        // receive buffer is empty between reads.
        if (!n) return false;
        channels_->request_counters_[fd_].next_to_read_.store(read + n, std::memory_order_release);
        next_rcv_valid_index_ += n;
        // Each request carries its own send time, there is no receive time.
        recv_callback_(this, 0);
        return true;
    }

    auto ShmSocket::flush() noexcept -> void {
        if (!next_send_valid_index_) return;
        const auto send_start = Common::rdtsc();
        const auto sent = ResponseRing::write(channels_->response_counters_[fd_], channels_->response_buffers_[fd_],
            send_buffer_, next_send_valid_index_);
        if (sent) {
            memmove(send_buffer_, send_buffer_ + sent, next_send_valid_index_ - sent);
            next_send_valid_index_ -= sent;
            // In the client's ring. As for TCP, by fd: the analysis takes the first write after a
            // response's T5, S0 is when it started.
            TRACE_AT(S0, fd_, send_start, logger_);
            TRACE(T6, fd_, logger_);
        }
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
            send_sockets_.erase(socket);
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
                // Allocated like a TCP accept. Its buffers are zeroed, so it doesn't page fault later.
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
        flush();
    }

    // A socket stays in send_sockets_ while its send buffer has data the client's ring had no room
    // for, the next flush() tries again.
    auto ShmServer::flush() noexcept -> void {
        send_sockets_.keepIf([this](ShmSocket* socket) {
            socket->flush();
            if (UNLIKELY(socket->send_disconnected_)) {
                markDisconnected(socket);
                return false;
            }
            return socket->next_send_valid_index_ != 0;
        });
    }

    // The next poll() drops it.
    auto ShmServer::markDisconnected(ShmSocket* socket) noexcept -> void {
        // ToDo - modernization
        // use c++20 find
        if (std::find(disconnected_sockets_.begin(), disconnected_sockets_.end(), socket) == disconnected_sockets_.end())
            disconnected_sockets_.push_back(socket);
    }
}
