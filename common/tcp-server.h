#pragma once
#include "tcp-socket.h"
namespace Common {
  struct TCPServer {
  public:
    int efd_ = -1;
    TCPSocket listener_socket_;
    epoll_event events_[1024];
    std::vector<TCPSocket *> sockets_, receive_sockets_,
      send_sockets_, disconnected_sockets_;
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
    private: 
        auto epoll_add(TCPSocket *socket) -> bool;
        auto epoll_del(TCPSocket *socket) -> bool;
  };
}