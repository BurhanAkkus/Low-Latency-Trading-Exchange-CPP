#pragma once
#include "tcp-socket.h"
#include "constants.h"
#include <array>
#include <set>
namespace Common {
  // Sockets in a list: contains, insert, erase and clear in O(1), iterated like an array. Each
  // socket keeps its place in the list in Index, one field per list: a socket can be in several.
  // Every slot below size_ holds a socket in the list, so a socket is in it when the slot its
  // Index points to holds it. A socket that left keeps a stale Index, which fails that check.
  // Erase moves the last socket into the gap, so the order changes. Don't erase while
  // iterating the same list, keepIf() removes while it iterates.
  template <size_t TCPSocket::*Index>
  struct SocketList {
    std::array<TCPSocket *, ME_MAX_NUM_CLIENTS> sockets_{};
    size_t size_ = 0;
    auto contains(const TCPSocket *socket) const noexcept {
      // Unsigned: index < size_ also keeps sockets_[index] in bounds.
      const size_t index = socket->*Index;
      return index < size_ && sockets_[index] == socket;
    }
    auto insert(TCPSocket *socket) noexcept -> void {
      if (contains(socket)) return;
      socket->*Index = size_;
      sockets_[size_++] = socket;
    }
    // The last socket takes the erased one's place.
    auto erase(TCPSocket *socket) noexcept -> void {
      if (!contains(socket)) return;
      TCPSocket *last = sockets_[--size_];
      sockets_[socket->*Index] = last;
      last->*Index = socket->*Index;
    }
    // Every socket leaves the list.
    auto clear() noexcept -> void { size_ = 0; }
    // Keeps the sockets keep() returns true for, in their order, the others leave the list.
    // keep() must not change the list.
    template <typename Keep>
    auto keepIf(Keep keep) noexcept -> void {
      size_t kept = 0;
      for (size_t i = 0; i < size_; ++i) {
        auto socket = sockets_[i];
        if (keep(socket)) { socket->*Index = kept; sockets_[kept++] = socket; }
      }
      size_ = kept;
    }
    auto begin() const noexcept { return sockets_.begin(); }
    auto end() const noexcept { return sockets_.begin() + size_; }
  };

  struct TCPServer {
  public:
    int efd_ = -1;
    TCPSocket listener_socket_;
    epoll_event events_[1024];
    std::vector<TCPSocket *> sockets_;
    // At most ME_MAX_NUM_CLIENTS connections, poll() refuses the rest.
    // receive_sockets_: the sockets epoll reported readable, sendAndRecv() reads them.
    // send_sockets_: the sockets with data in their send buffer, send() adds them, flush() sends
    // them.
    SocketList<&TCPSocket::receive_index_> receive_sockets_;
    SocketList<&TCPSocket::send_index_> send_sockets_;
    std::set<TCPSocket *> disconnected_sockets_;
    std::function<void(TCPSocket *s, Nanos rx_time)> recv_callback_;
    std::function<void()> recv_finished_callback_;
    std::function<void(TCPSocket* socket)> disconnect_callback_;
    std::string time_str_;
    Logger &logger_;

    explicit TCPServer(Logger &logger,
      std::function<void(TCPSocket *s, Nanos rx_time)> recv_callback,
      std::function<void()> recv_finished_callback)
        : listener_socket_(logger), recv_callback_(std::move(recv_callback)),
          recv_finished_callback_(std::move(recv_finished_callback)), logger_(logger) {
      disconnect_callback_ = [](auto) {};
    }


    // Delete Defaults
    TCPServer() = delete; // Default ctor.
    TCPServer(const TCPServer&) = delete; // Copy Ctor.
    TCPServer& operator=(const TCPServer&) = delete; // Copy Assignment.
    TCPServer(const TCPServer&&) = delete; // Move Ctor.
    TCPServer& operator=(const TCPServer&&) = delete; // Move Assignment;

    auto listen(const std::string &iface, int port) -> void;
    auto del(TCPSocket* socket) -> void;

    auto destroy() noexcept -> void;
    ~TCPServer();
    
    auto poll() noexcept -> void;
    auto sendAndRecv() noexcept -> void;
    // Into the socket's send buffer, the next flush() sends it.
    auto send(TCPSocket *socket, const void *data, size_t len) noexcept -> void {
      socket->send(data, len);
      send_sockets_.insert(socket);
    }
    // Sends the send buffers of send_sockets_.
    auto flush() noexcept -> void;
    private:
        auto epoll_add(TCPSocket *socket) -> bool;
        auto epoll_del(TCPSocket *socket) -> bool;
        auto markIfDisconnected(TCPSocket *socket) noexcept -> void;
  };
}