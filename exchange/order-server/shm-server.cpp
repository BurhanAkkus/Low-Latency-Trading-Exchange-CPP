#include "exchange/order-server/shm-server.h"
#include "common/perf-utils.h"
#include "common/time-utils.h"

namespace Exchange{
    auto ShmServer::send(Slot slot, const void* data, size_t len) noexcept -> void {
        if (UNLIKELY(slots_[slot] != SlotState::CONNECTED)) return;
        // const auto send_start = Common::rdtsc();
        const auto sent = ResponseRing::write(channels_->response_counters_[slot], channels_->response_buffers_[slot],
            data, len);
        // The client doesn't read what it's sent: drop it rather than lose data or stall everyone.
        // A part that fit stays unread, the client reads whole responses only.
        if (UNLIKELY(sent != len)) {
            markDisconnected(slot, SlotState::DROPPED);
            return;
        }
        // In the client's ring. As for TCP, by fd: the analysis took the first write after a
        // response's T5, S0 is when it started. T6 is logged per final response now, by
        // OrderServer::sendClientResponses.
        // TRACE_AT(S0, slot, send_start, logger_);
        // TRACE(T6, slot, logger_);
    }

    auto ShmServer::receive(Slot slot, size_t read, size_t written) noexcept -> bool {
        const auto& buffer = channels_->request_buffers_[slot];
        // One request at a time, copied out whole: it can wrap around the ring's end.
        OMClientRequest request;
        auto next = read;
        auto taken = true;
        while (RequestRing::read(buffer, next, written, &request, sizeof(request))) {
            if (UNLIKELY(!recv_callback_(slot, request))) {
                taken = false;
                break;
            }
            next += sizeof(request);
        }
        if (next != read) channels_->request_counters_[slot].next_to_read_.store(next, std::memory_order_release);
        return taken;
    }

    auto ShmServer::listen() -> void {
        region_ = std::make_unique<SharedRegion<OrderChannels>>(ORDER_CHANNELS_NAME, SharedRegion<OrderChannels>::Open::CREATE);
        channels_ = region_->get();
        logger_.log("%:% %() % shared memory % ready for % clients\n", __FILE__, __LINE__, __FUNCTION__,
            Common::getCurrentTimeStr(&time_str_), ORDER_CHANNELS_NAME, ME_MAX_NUM_CLIENTS);
    }

    auto ShmServer::poll() noexcept -> void {
        auto& states = channels_->states_;
        for (const auto slot : disconnected_) {
            disconnect_callback_(slot);
            // The slot isn't reused in this run: it stays CLOSED with its rings as they are, a new
            // client there would read the old one's leftover responses. Dropped by us, the client
            // sees CLOSED too.
            states[slot].state_.store(ClientState::CLOSED, std::memory_order_release);
            logger_.log("%:% %() % dropped client slot:% closed by the client:%\n", __FILE__, __LINE__, __FUNCTION__,
                Common::getCurrentTimeStr(&time_str_), slot, slots_[slot] == SlotState::CLOSED);
        }
        disconnected_.clear();
        // Clients count every CONNECTED and CLOSED: nothing to rescan until it moves.
        const auto changes = channels_->changes_.load(std::memory_order_acquire);
        if (LIKELY(changes == seen_changes_)) return;
        seen_changes_ = changes;
        auto accepted = false;
        for (Slot slot = 0; slot < states.size(); ++slot) {
            const auto state = states[slot].state_.load(std::memory_order_acquire);
            // ToDo - performance
            // early exit.
            if (state == ClientState::CONNECTED && slots_[slot] == SlotState::NONE) {
                // Picked up like a TCP accept.
                slots_[slot] = SlotState::CONNECTED;
                accepted = true;
                logger_.log("%:% %() % client connected slot:%\n", __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str_), slot);
            } else if (state == ClientState::CLOSED) {
                markDisconnected(slot, SlotState::CLOSED);
            }
        }
        if (accepted) rebuildConnected();
    }

    auto ShmServer::sendAndRecv() noexcept -> void {
        // When this pass started: P1 for the analysis, logged only for passes that read something.
        // const auto pass_start = Common::rdtsc();
        // Phase 1: which clients have a whole request to read. Only the CONNECTED slots' request
        // counters, in slot order: a client with nothing new costs two loads, a slot without one
        // nothing. No slot changes state during a pass, sends drop clients only after it.
        const auto& counters = channels_->request_counters_;
        size_t to_read = 0;
        for (size_t i = 0; i < connected_count_; ++i) {
            const auto slot = connected_[i];
            const auto written = counters[slot].next_to_write_.load(std::memory_order_acquire);
            // Only we store it.
            const auto read = counters[slot].next_to_read_.load(std::memory_order_relaxed);
            if (written - read >= sizeof(OMClientRequest)) to_read_[to_read++] = {slot, read, written};
        }
        // Phase 2: read them, up to what phase 1 saw.
        size_t received = 0;
        for (size_t i = 0; i < to_read; ++i) {
            const auto& [slot, read, written] = to_read_[i];
            ++received;
            // The order server has no room: the rest waits in the rings for the next pass.
            if (UNLIKELY(!receive(slot, read, written))) break;
        }
        if (received) {
            // TRACE_AT(P1, received, pass_start, logger_);
            recv_finished_callback_();
        }
    }

    auto ShmServer::markDisconnected(Slot slot, SlotState state) noexcept -> void {
        // Listed once: only a CONNECTED slot changes.
        if (slots_[slot] != SlotState::CONNECTED) return;
        slots_[slot] = state;
        disconnected_.push_back(slot);
        // Out of the pass from now on.
        rebuildConnected();
    }

    auto ShmServer::rebuildConnected() noexcept -> void {
        connected_count_ = 0;
        for (Slot slot = 0; slot < slots_.size(); ++slot)
            if (slots_[slot] == SlotState::CONNECTED) connected_[connected_count_++] = slot;
    }
}
