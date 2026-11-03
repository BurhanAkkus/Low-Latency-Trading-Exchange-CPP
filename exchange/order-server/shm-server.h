#pragma once
#include "common/logging.h"
#include "common/shared-region.h"
#include "exchange/order-server/order-channels.h"
#include <functional>
#include <memory>
#include <vector>

namespace Exchange{
    // One client of the shared memory transport, in place of a TCPSocket: its requests come out
    // of its requests ring one at a time into recv_callback_, its responses go straight into its
    // responses ring.
    struct ShmSocket{
        // The client's slot: the trace key in place of a socket's fd (T6fd, S0/T6).
        int fd_ = -1;
        // The region in this process, the client's rings are index fd_ of its arrays.
        OrderChannels* channels_ = nullptr;
        // The order server drops it: a response didn't fit in its ring, or the client CLOSED its slot.
        bool send_disconnected_ = false;
        bool closed_by_client_ = false;
        // A request of the client. False if the order server has no room for it now.
        std::function<bool(ShmSocket* s, const OMClientRequest& request)> recv_callback_;
        Logger& logger_;

        explicit ShmSocket(Logger& logger) : logger_(logger) {}

        // Delete defaults.
        ShmSocket() = delete; // Default ctor
        ShmSocket(const ShmSocket&) = delete; // Copy ctor
        ShmSocket& operator=(const ShmSocket&) = delete; // Copy assignment
        ShmSocket(const ShmSocket&&) = delete; // Move ctor
        ShmSocket& operator=(const ShmSocket&&) = delete; // Move assignment

        // Into the client's responses ring. False if it didn't all fit, or the client is already
        // to be dropped.
        auto send(const void* data, size_t len) noexcept -> bool;
        // Reads the client's requests from read up to written, its ring's counters as the pass
        // loaded them, and calls recv_callback_ with each. False if recv_callback_ had no room for
        // one: it and the ones after it stay in the ring.
        auto receive(size_t read, size_t written) noexcept -> bool;
    };

    // The order server's transport over shared memory, in place of TCPServer, with the same calls
    // but flush(): send() writes straight into the client's ring. listen() creates the
    // OrderChannels region. A client connects by setting its slot CONNECTED, poll() picks that up
    // like an accept. sendAndRecv() scans every slot's request counters, then reads only the
    // clients that wrote something: no syscall, two loads for a client with nothing new.
    class ShmServer{
        public:
        std::function<void(ShmSocket* socket)> disconnect_callback_;

        ShmServer(Logger& logger, std::function<bool(ShmSocket* s, const OMClientRequest& request)> recv_callback,
            std::function<void()> recv_finished_callback)
            : recv_callback_(std::move(recv_callback)), recv_finished_callback_(std::move(recv_finished_callback)),
              logger_(logger) {
            disconnect_callback_ = [](auto) {};
        }
        ~ShmServer();

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
        // Into the socket's responses ring. A client it didn't fit is dropped by the next poll().
        auto send(ShmSocket* socket, const void* data, size_t len) noexcept -> void {
            if (UNLIKELY(!socket->send(data, len))) markDisconnected(socket);
        }

        private:
        std::function<bool(ShmSocket* s, const OMClientRequest& request)> recv_callback_;
        std::function<void()> recv_finished_callback_;
        Logger& logger_;
        std::string time_str_;
        std::unique_ptr<SharedRegion<OrderChannels>> channels_;
        // The last changes_ seen: the slots' states are rescanned when it moves.
        uint32_t seen_changes_ = 0;
        std::array<ShmSocket*, ME_MAX_NUM_CLIENTS> by_slot_{};
        // Connected clients.
        std::vector<ShmSocket*> sockets_;
        // The pass's phase 1 lists the clients that wrote something, phase 2 reads them.
        struct ToRead{
            size_t slot_;
            size_t read_;
            size_t written_;
        };
        std::array<ToRead, ME_MAX_NUM_CLIENTS> to_read_;
        // Dropped by the next poll().
        std::vector<ShmSocket*> disconnected_sockets_;

        auto markDisconnected(ShmSocket* socket) noexcept -> void;
    };
}
