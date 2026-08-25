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
    std::string time_str_;
    Logger &logger_;


    inline auto defaultRecvCallback(TCPSocket *socket, Nanos
      rx_time) noexcept {
      logger_.log("%:% %() % TCPServer::defaultRecvCallback() socket:% len:% rx:%\n",
         __FILE__, __LINE__, __FUNCTION__,
            Common::getCurrentTimeStr(&time_str_), socket->
              fd_, socket->next_rcv_valid_index_, rx_time);
    }
    inline auto defaultRecvFinishedCallback() noexcept {
      logger_.log("%:% %() % TCPServer:: defaultRecvFinishedCallback()\n", 
        Common::getCurrentTimeStr(&time_str_));
    }


    explicit TCPServer(Logger &logger)
        : listener_socket_(logger), logger_(logger) {
      recv_callback_ = [this](auto socket, auto rx_time) {
        defaultRecvCallback(socket, rx_time); };
      recv_finished_callback_ = [this]() {
        defaultRecvFinishedCallback(); };
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