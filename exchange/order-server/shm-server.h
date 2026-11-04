#pragma once
#include "common/logging.h"
#include "common/shared-region.h"
#include "exchange/order-server/order-channels.h"
#include <functional>
#include <limits>
#include <memory>
#include <vector>

namespace Exchange{
    // A client in the order server's calls, in place of a socket's fd: its slot in OrderChannels,
    // its rings are that index of the arrays. Also its trace key (T6fd, S0/T6).
    using Slot = size_t;
    constexpr Slot NO_SLOT = std::numeric_limits<Slot>::max();

    // A slot as the order server sees it. DROPPED and CLOSED stay until the next run: the slot
    // isn't reused.
    enum class SlotState : uint8_t{
        // No client yet, or poll() hasn't picked it up.
        NONE = 0,
        CONNECTED = 1,
        // A response didn't fit in its ring.
        DROPPED = 2,
        // The client CLOSED it.
        CLOSED = 3
    };

    // The order server's transport over shared memory, in place of TCPServer, with the same calls
    // but flush(): send() writes straight into the client's ring. listen() creates the
    // OrderChannels region. A client connects by setting its slot CONNECTED, poll() picks that up
    // like an accept. sendAndRecv() scans every slot's request counters, then reads only the
    // clients that wrote something: no syscall, two loads for a client with nothing new. Only
    // CONNECTED slots are read and sent to.
    class ShmServer{
        public:
        std::function<void(Slot slot)> disconnect_callback_;

        // recv_callback: a request of the slot's client. False if the order server has no room for
        // it now: it and the ones after it stay in the ring, as unread bytes stay in a TCP socket.
        ShmServer(Logger& logger, std::function<bool(Slot slot, const OMClientRequest& request)> recv_callback,
            std::function<void()> recv_finished_callback)
            : recv_callback_(std::move(recv_callback)), recv_finished_callback_(std::move(recv_finished_callback)),
              logger_(logger) {
            disconnect_callback_ = [](auto) {};
        }

        // Delete defaults.
        ShmServer() = delete; // Default ctor
        ShmServer(const ShmServer&) = delete; // Copy ctor
        ShmServer& operator=(const ShmServer&) = delete; // Copy assignment
        ShmServer(const ShmServer&&) = delete; // Move ctor
        ShmServer& operator=(const ShmServer&&) = delete; // Move assignment

        // Creates the region.
        auto listen() -> void;
        // Drops the clients disconnected since the last call, picks up new ones.
        auto poll() noexcept -> void;
        // Reads every client with a whole request in its ring. Nothing to send, send() did.
        auto sendAndRecv() noexcept -> void;
        // Into the slot's responses ring. A client it didn't all fit is dropped by the next poll().
        auto send(Slot slot, const void* data, size_t len) noexcept -> void;

        private:
        std::function<bool(Slot slot, const OMClientRequest& request)> recv_callback_;
        std::function<void()> recv_finished_callback_;
        Logger& logger_;
        std::string time_str_;
        std::unique_ptr<SharedRegion<OrderChannels>> region_;
        // The region's OrderChannels in this process, as the socket kept it: one load per use,
        // not region_'s pointer and then its base address.
        OrderChannels* channels_ = nullptr;
        // The last changes_ seen: the slots' states are rescanned when it moves.
        uint32_t seen_changes_ = 0;
        // One byte per slot, the sends check it.
        std::array<SlotState, ME_MAX_NUM_CLIENTS> slots_{};
        // The CONNECTED slots in slot order, the only ones the pass looks at. Rebuilt from slots_
        // when a slot becomes CONNECTED or stops being it.
        std::array<Slot, ME_MAX_NUM_CLIENTS> connected_{};
        size_t connected_count_ = 0;
        // The pass's phase 1 lists the clients that wrote something, phase 2 reads them.
        struct ToRead{
            size_t slot_;
            size_t read_;
            size_t written_;
        };
        std::array<ToRead, ME_MAX_NUM_CLIENTS> to_read_;
        // DROPPED or CLOSED since the last poll(), it drops them.
        std::vector<Slot> disconnected_;

        // Reads the slot's requests from read up to written, its ring's counters as the pass
        // loaded them, and calls recv_callback_ with each. False if recv_callback_ had no room for
        // one.
        auto receive(Slot slot, size_t read, size_t written) noexcept -> bool;
        // A CONNECTED slot becomes state (DROPPED or CLOSED), the next poll() drops it.
        auto markDisconnected(Slot slot, SlotState state) noexcept -> void;
        auto rebuildConnected() noexcept -> void;
    };
}
