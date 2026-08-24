#pragma once
#include <functional>
#include "socket-utils.h"
#include "logging.h"
namespace Common {
  constexpr size_t TCPBufferSize = 64 * 1024 * 1024;
  struct TCPSocket {
    int fd_ = -1;
    char *send_buffer_ = nullptr;
    size_t next_send_valid_index_ = 0;
    char *rcv_buffer_ = nullptr;
    size_t next_rcv_valid_index_ = 0;
    bool send_disconnected_ = false;
    bool recv_disconnected_ = false;
    struct sockaddr_in inInAddr;
    std::function<void(TCPSocket *s, Nanos rx_time)>recv_callback_;
    std::string time_str_;
    Logger &logger_;
    auto defaultRecvCallback(TCPSocket *socket, Nanos
      rx_time) noexcept {
      logger_.log("%:% %() %TCPSocket::defaultRecvCallback() socket:% len:%rx:%\n",
         __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_),
                    socket->fd_, socket->next_rcv_valid_index_, rx_time);
    }
    // Allocates buffers from heap.
    // Can't dynamically create TCPSockets.
    // Only compile-time creation or Allocate to MemoryPool.
    explicit TCPSocket(Logger &logger)
        : logger_(logger) {
      send_buffer_ = new char[TCPBufferSize];
      rcv_buffer_ = new char[TCPBufferSize];
      recv_callback_ = [this](auto socket, auto rx_time) {
        defaultRecvCallback(socket, rx_time); };
    }
    // Dtor
    auto destroy() noexcept -> void {
        close(fd_);
        fd_ = -1;
    }
    ~TCPSocket() {
        destroy();
        delete[] send_buffer_; send_buffer_ = nullptr;
        delete[] rcv_buffer_; rcv_buffer_ = nullptr;
    }

    // Delete defaults.
    TCPSocket() = delete; // Default ctor
    TCPSocket(const TCPSocket&) = delete; // Copy Ctor
    TCPSocket& operator=(const TCPSocket&) = delete; // Copy assignment
    TCPSocket(const TCPSocket&&) = delete; // Move Ctor
    TCPSocket& operator=(const TCPSocket&&) = delete; // Move assignment

    // Create TCPSocket with provided attributes to either listen-on / connect-to.
    auto connect(const std::string &ip, const std::string &iface, int port, bool is_listening) -> int;

    // Write outgoing data to the send buffer.
    auto send(const void *data, size_t len) noexcept -> void;
    
    // Called to publish outgoing data from the buffers as well as check for and callback if data is available in the read buffers.
    auto sendAndRecv() noexcept -> bool;
};
  
}