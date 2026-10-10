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

    auto TCPServer::epoll_del(TCPSocket *socket) -> bool {
        return (epoll_ctl(efd_, EPOLL_CTL_DEL, socket->fd_,
        nullptr) != -1);
    }

    auto TCPServer::del(TCPSocket* socket) -> void{
        epoll_del(socket);
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

    auto TCPServer::poll() noexcept -> void {
        // events_ has size 1024. can't write past it.
        const int max_events = std::min(1 + sockets_.size(), 1024ul);
        // drop disconnected sockets to reduce runtime down the function.
        for (auto socket: disconnected_sockets_) {
            TCPServer::del(socket);
            // stalls thread.
            delete socket;
        }
        disconnected_sockets_.clear();
        // number of ready to interact sockets.
        const int n = epoll_wait(efd_, events_, max_events, 0);
        // iterate over each.
        bool have_new_connection = false;
        for (int i = 0; i < n; ++i) {
            epoll_event &event = events_[i];
            // event.data.ptr points to the socket during epoll_add.
            auto socket = reinterpret_cast<TCPSocket*>(event.data.ptr);
            // if event is EPOLLIN
            if(event.events & EPOLLIN){
                // if its a new connection
                if(UNLIKELY(socket == &listener_socket_)){
                    //set have_new_connection
                    logger_.log("%:% %() % EPOLLIN listener_socket:%\n", 
                        __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_), socket->fd_);
                    have_new_connection = true;
                    continue;
                }
                logger_.log("%:% %() % EPOLLIN socket:%\n",
                    __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentNanos(), 
                    socket->fd_);
                // add to receive sockets
                if(std::find(receive_sockets_.begin(),receive_sockets_.end(), socket) == receive_sockets_.end())
                    receive_sockets_.push_back(socket);
            }
            // if event is EPOLLOUT
            if(event.events & EPOLLOUT){
                logger_.log("%:% %() % EPOLLOUT socket:%\n",
                    __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentNanos(), 
                    socket->fd_);
                // add to send sockets
                if(std::find(send_sockets_.begin(),send_sockets_.end(), socket) == send_sockets_.end())
                    send_sockets_.push_back(socket);
            }
            // if connection is dropped
            if (event.events & (EPOLLERR | EPOLLHUP)) {
                logger_.log("%:% %() % EPOLLERR socket:%\n",
                __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str_), socket->fd_);
                if(std::find(disconnected_sockets_.begin(),
                    disconnected_sockets_.end(), socket) ==
                    disconnected_sockets_.end())
                disconnected_sockets_.push_back(socket);
            }
        }
        while (have_new_connection) {
            sockaddr_storage sockaddr_;
            socklen_t socklen = sizeof(sockaddr_);
            auto fd = accept(listener_socket_.fd_,reinterpret_cast<sockaddr*>(&sockaddr_),&socklen);
            // Ignore errors. Assume accept never fails when there is a pending connection.
            // ToDo
            // error log. 
            if (fd == -1){
                break;
            }
            ASSERT(setNoDelay(fd) && setNonBlocking(fd),
            "Failed to set fd to no delay and non blocking" + std::to_string(fd));
            logger_.log("%:% %() % accepted socket:%\n",
                __FILE__, __LINE__, __FUNCTION__,
                Common::getCurrentTimeStr(&time_str_), fd);
            // ToDo
            // dynamic allocation of TCPSocket..
            // migrate to MemoryPool if required in critical path
            // stalls poll, should be moved.
            // Setup socket.
            auto socket = new TCPSocket(logger_);
            socket->fd_ = fd;
            socket->recv_callback_ = recv_callback_;
            // Add it to tracked sockets.
            ASSERT(epoll_add(socket), "Couldn't epoll add socket. Error: "+ std::to_string(errno));
            if(std::find(sockets_.cbegin(),sockets_.cend(),socket) == sockets_.cend()){
                sockets_.push_back(socket);
            }
            if(std::find(receive_sockets_.cbegin(),receive_sockets_.cend(),socket) == receive_sockets_.cend()){
                receive_sockets_.push_back(socket);
            }
        }
    }

    auto TCPServer::sendAndRecv() noexcept -> void {
        auto recv = false;
        for (auto socket: receive_sockets_) {
            if(socket->sendAndRecv())
                recv = true;
            }
        if(recv)
            recv_finished_callback_();  
        for (auto socket: send_sockets_) {
            socket->sendAndRecv();
        }
    }
}