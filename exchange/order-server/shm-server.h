#pragma once
#include "common/logging.h"
#include "common/shared-region.h"
#include "common/socket-list.h"
#include "exchange/order-server/order-channels.h"
#include <functional>
#include <limits>
#include <memory>
#include <vector>

namespace Exchange{
    // One client of the shared memory transport, in place of a TCPSocket: its requests come out
    // of its requests ring into rcv_buffer_, its responses go into send_buffer_ and flush() moves
    // them into its responses ring.
    struct ShmSocket{
        // The client's slot: the trace key in place of a socket's fd (T6fd, S0/T6).
        int fd_ = -1;
        // The region in this process, the client's rings are index fd_ of its arrays.
        OrderChannels* channels_ = nullptr;
        // Whole requests only, so nothing is left over between reads.
        static constexpr size_t RECEIVE_BUFFER_SIZE = ORDER_RING_SIZE;
        // Responses that didn't fit in the client's ring yet. A client this far behind is dropped.
        // ToDo - correctness
        // every response passes through here before flush(): a client getting more than
        // SEND_BUFFER_SIZE of responses in one drain is dropped even with an empty ring. Flush
        // into the ring when this fills up instead.
        static constexpr size_t SEND_BUFFER_SIZE = 2 * ORDER_RING_SIZE;
        // Zeroed with the socket: its pages are in before the client's first request.
        // ToDo - performance
        // ring buffer instead of moving.
        // ToDo - performance
        // fields every pass reads first, buffers last: next_rcv_valid_index_ after the 64 KB
        // rcv_buffer_ puts it on another page, a pass over 255 clients touches 255 more pages.
        // Moved up, the response path was ~0.3 us faster (2.64-2.82 vs 3.03-3.07 us mean).
        char rcv_buffer_[RECEIVE_BUFFER_SIZE]{};
        size_t next_rcv_valid_index_ = 0;
        char send_buffer_[SEND_BUFFER_SIZE]{};
        size_t next_send_valid_index_ = 0;
        // The order server drops it: its send buffer overflowed, or the client CLOSED its slot.
        bool send_disconnected_ = false;
        bool closed_by_client_ = false;
        // Its place in the server's send list.
        size_t send_index_ = std::numeric_limits<size_t>::max();
        std::function<void(ShmSocket* s, Nanos rx_time)> recv_callback_;
        Logger& logger_;

        explicit ShmSocket(Logger& logger) : logger_(logger) {}

        // Delete defaults.
        ShmSocket() = delete; // Default ctor
        ShmSocket(const ShmSocket&) = delete; // Copy ctor
        ShmSocket& operator=(const ShmSocket&) = delete; // Copy assignment
        ShmSocket(const ShmSocket&&) = delete; // Move ctor
        ShmSocket& operator=(const ShmSocket&&) = delete; // Move assignment

        // Into the send buffer.
        auto send(const void* data, size_t len) noexcept -> void;
        // Reads the client's new requests into the receive buffer and calls recv_callback_. True if
        // there were any.
        auto receive() noexcept -> bool;
        // Moves the send buffer into the client's responses ring, as much as fits, the rest stays
        // for the next call.
        auto flush() noexcept -> void;
    };

    // The order server's transport over shared memory, in place of TCPServer, with the same calls.
    // listen() creates the OrderChannels region. A client connects by setting its slot CONNECTED,
    // poll() picks that up like an accept. sendAndRecv() checks every connected client's ring: no
    // syscall, a load of the ring's counter for a client with nothing new.
    class ShmServer{
        public:
        std::function<void(ShmSocket* socket)> disconnect_callback_;

        ShmServer(Logger& logger, std::function<void(ShmSocket* s, Nanos rx_time)> recv_callback,
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
        // Reads every connected client, then sends what's in the send buffers.
        auto sendAndRecv() noexcept -> void;
        // Into the socket's send buffer, the next flush() sends it.
        auto send(ShmSocket* socket, const void* data, size_t len) noexcept -> void {
            socket->send(data, len);
            send_sockets_.insert(socket);
        }
        // Sends the send buffers of send_sockets_.
        auto flush() noexcept -> void;

        private:
        std::function<void(ShmSocket* s, Nanos rx_time)> recv_callback_;
        std::function<void()> recv_finished_callback_;
        Logger& logger_;
        std::string time_str_;
        std::unique_ptr<SharedRegion<OrderChannels>> channels_;
        // The last changes_ seen: the slots' states are rescanned when it moves.
        uint32_t seen_changes_ = 0;
        std::array<ShmSocket*, ME_MAX_NUM_CLIENTS> by_slot_{};
        // Connected clients, read on every pass.
        std::vector<ShmSocket*> sockets_;
        // Sockets with data in their send buffer, send() adds them, flush() sends them.
        SocketList<ShmSocket, &ShmSocket::send_index_> send_sockets_;
        // Dropped by the next poll().
        std::vector<ShmSocket*> disconnected_sockets_;

        auto markDisconnected(ShmSocket* socket) noexcept -> void;
    };
}
