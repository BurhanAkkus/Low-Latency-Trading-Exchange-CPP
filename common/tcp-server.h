#pragma once
#include "tcp-socket.h"
#include "socket-list.h"
#include <set>
namespace Common {
  struct TCPServer {
  public:
    int efd_ = -1;
    TCPSocket listener_socket_;
    epoll_event events_[1024];
    std::vector<TCPSocket *> sockets_;
    // At most ME_MAX_NUM_CLIENTS connections, poll() refuses the rest.
    // receive_sockets_: the sockets epoll reported readable, sendAndRecv() reads them and empties
    // the list. poll() pushes without contains, epoll reports a socket once per poll(), so
    // sendAndRecv() has to run between two poll()s.
    // send_sockets_: the sockets with data in their send buffer, send() adds them, flush() sends
    // them.
    SocketList<TCPSocket, &TCPSocket::receive_index_> receive_sockets_;
    SocketList<TCPSocket, &TCPSocket::send_index_> send_sockets_;
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