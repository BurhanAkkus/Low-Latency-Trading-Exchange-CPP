#include "tcp-server.h"
#include <algorithm>
namespace Common{

    auto TCPServer::destroy() noexcept -> void{
        close(efd_);
        efd_ = -1;
        listener_socket_.destroy();
    }
    TCPServer::~TCPServer() {
        destroy();
    }

    auto TCPServer::epoll_add(TCPSocket* socket) -> bool{
        epoll_event event;
        // reconstruct socket via reinterpret_cast<TCPSocket*>(event.data.ptr).
        event.data.ptr = reinterpret_cast<void *>(socket);
        event.events = EPOLLIN | EPOLLET;
        return epoll_ctl(efd_, EPOLL_CTL_ADD,socket->fd_,&event) == 0;
    }

    auto TCPServer::epoll_del(TCPSocket* socket) -> void{
        epoll_ctl(efd_, EPOLL_CTL_DEL,socket->fd_,nullptr);
        // remove socket from TCPServer bookkeeping.
        // ToDo
        // Search is in O(n). Can be improved?
        sockets_.erase(std::find(sockets_.cbegin(),sockets_.cend(),socket),sockets_.end());
        receive_sockets_.erase(std::find(receive_sockets_.cbegin(),receive_sockets_.cend(),socket),receive_sockets_.end());
        send_sockets_.erase(std::find(send_sockets_.cbegin(),send_sockets_.cend(),socket),send_sockets_.end());
    }
 
    auto TCPServer::listen(const std::string &iface, int port) -> void {
        destroy();
        efd_ = epoll_create(1);
        ASSERT(efd_ >= 0, "epoll_create() failed error:" +
            std::string(std::strerror(errno)));
        ASSERT(listener_socket_.connect("", iface, port, true) >= 0,
            "Listener socket failed to connect. iface:" + iface +
             " port:" + std::to_string(port) + "error:" + std::string
                (std::strerror(errno)));
        ASSERT(TCPServer::epoll_add(&listener_socket_), "epoll_ctl() failed. error:" + std::string(std::strerror(errno)));
    }
}