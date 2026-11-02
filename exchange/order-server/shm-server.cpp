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

    auto ShmSocket::receive() noexcept -> bool {
        // Nothing new costs a load of the ring's counter. The read's start time only for a ring
        // that has something: a TSC read for each of 255 clients on every pass is a microsecond.
        if (!channel_->requests_.readable()) return false;
        const auto recv_start = Common::rdtsc();
        const auto n = channel_->requests_.read(rcv_buffer_ + next_rcv_valid_index_, RECEIVE_BUFFER_SIZE - next_rcv_valid_index_);
        if (!n) return false;
        // The read from the ring started and returned. By fd, like T6: the analysis takes the last
        // read before a request's T1.
        TRACE_AT(R0, fd_, recv_start, logger_);
        TRACE(R1, fd_, logger_);
        next_rcv_valid_index_ += n;
        // Each request carries its own send time, there is no receive time.
        recv_callback_(this, 0);
        return true;
    }

    auto ShmSocket::flush() noexcept -> void {
        if (!next_send_valid_index_) return;
        const auto send_start = Common::rdtsc();
        const auto sent = channel_->responses_.write(send_buffer_, next_send_valid_index_);
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
            socket->channel_->state_.store(ClientState::CLOSED, std::memory_order_release);
            logger_.log("%:% %() % dropped client slot:% closed by the client:%\n", __FILE__, __LINE__, __FUNCTION__,
                Common::getCurrentTimeStr(&time_str_), socket->fd_, socket->closed_by_client_);
            delete socket;
        }
        disconnected_sockets_.clear();
        // Clients count every CONNECTED and CLOSED: nothing to rescan until it moves.
        const auto changes = channels_->get()->changes_.load(std::memory_order_acquire);
        if (LIKELY(changes == seen_changes_)) return;
        seen_changes_ = changes;
        auto& clients = channels_->get()->clients_;
        for (size_t slot = 0; slot < clients.size(); ++slot) {
            const auto state = clients[slot].state_.load(std::memory_order_acquire);
            auto socket = by_slot_[slot];
            // ToDo - performance
            // early exit.
            if (state == ClientState::CONNECTED && !socket) {
                // Allocated like a TCP accept. Its buffers are zeroed, so it doesn't page fault later.
                socket = new ShmSocket(logger_);
                socket->fd_ = static_cast<int>(slot);
                socket->channel_ = &clients[slot];
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
        size_t received = 0;
        for (auto socket : sockets_)
            if (socket->receive()) ++received;
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
