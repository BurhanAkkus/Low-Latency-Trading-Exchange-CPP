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
        // Level-triggered: epoll_wait reports a socket for as long as it has data to read, so a
        // socket can leave receive_sockets_ once it's read.
        event.events = EPOLLIN;
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
        std::erase(sockets_,socket);
        receive_sockets_.erase(socket);
        send_sockets_.erase(socket);
        disconnect_callback_(socket);
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
        // ToDo - improve
        // keep them in order to reduce search time.
        for (auto socket: disconnected_sockets_) {
            TCPServer::del(socket);
            // stalls thread.
            delete socket;
        }
        disconnected_sockets_.clear();
        // number of ready to interact sockets.
        const int n = epoll_wait(efd_, events_, max_events, 0);
        // iterate over each.
        bool has_new_connection{false};
        for (int i = 0; i < n; ++i) {
            epoll_event &event = events_[i];
            // event.data.ptr points to the socket during epoll_add.
            auto socket = reinterpret_cast<TCPSocket*>(event.data.ptr);
            // The listener is readable when connections are waiting to be accepted.
            if (UNLIKELY(socket == &listener_socket_)) {
                has_new_connection = true;
                continue;
            }
            // Something to read, or the peer closed: recv returns 0.
            if (event.events & EPOLLIN) receive_sockets_.push(socket);
            // The kernel's send buffer has room again.
            if (event.events & EPOLLOUT) send_sockets_.insert(socket);
            // The connection is gone.
            if (event.events & (EPOLLERR | EPOLLHUP)) disconnected_sockets_.insert(socket);
        }
        if(has_new_connection) {
            sockaddr_storage sockaddr_;
            socklen_t socklen = sizeof(sockaddr_);
            auto fd = accept(listener_socket_.fd_,reinterpret_cast<sockaddr*>(&sockaddr_),&socklen);
            // No room for another connection: refuse it.
            if (UNLIKELY(sockets_.size() >= ME_MAX_NUM_CLIENTS)) {
                logger_.log("%:% %() % refused socket:%, % connections open\n",
                    __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str_), fd, sockets_.size());
                close(fd);
            }
            ASSERT(setNoDelay(fd) && setNonBlocking(fd),
            "Failed to set fd to no delay and non blocking" + std::to_string(fd));
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
            // New: epoll_wait above didn't know it yet.
            receive_sockets_.push(socket);
        }
    }

    auto TCPServer::sendAndRecv() noexcept -> void {
        auto recv = false;
        for (auto socket: receive_sockets_) {
            if(socket->receive())
                recv = true;
            markIfDisconnected(socket);
            }
        if(recv)
            recv_finished_callback_();
        flush();
        // Read. epoll reports them again when there is more.
        receive_sockets_.clear();
    }
    // A socket stays in send_sockets_ while its send buffer has data the kernel didn't take, the
    // next flush() tries again.
    auto TCPServer::flush() noexcept -> void {
        send_sockets_.keepIf([this](TCPSocket *socket) {
            socket->flush();
            markIfDisconnected(socket);
            return socket->next_send_valid_index_ != 0 && !socket->send_disconnected_;
        });
    }
    // The next poll() deletes it.
    auto TCPServer::markIfDisconnected(TCPSocket *socket) noexcept -> void {
        if (UNLIKELY(socket->send_disconnected_ || socket->recv_disconnected_))
            disconnected_sockets_.insert(socket);
    }
}