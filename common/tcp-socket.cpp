#include "tcp-socket.h"

namespace Common {
    // Create TCPSocket with provided attributes to either listen-on / connect-to.
    auto TCPSocket::connect(const std::string &ip, const
        std::string &iface, int port, bool is_listening) -> int {
        TCPSocket::destroy();
        fd_ = createSocket(logger_, ip, iface, port, false,
        false, is_listening, 0, true);
        // wahats inInAddr doing?
        inInAddr.sin_addr.s_addr = INADDR_ANY;
        inInAddr.sin_port = htons(port);
        inInAddr.sin_family = AF_INET;
        return fd_;
    }
    // Push to send buffer.
    auto TCPSocket::send(const void *data, size_t len) noexcept -> void {
        // The peer doesn't read what we send: disconnect it rather than lose data or stall everyone.
        if (UNLIKELY(send_disconnected_ || next_send_valid_index_ + len > TCPBufferSize)) {
            send_disconnected_ = true;
            return;
        }
        memcpy(send_buffer_ + next_send_valid_index_, data, len);
        next_send_valid_index_ += len;
    }
    // receive and Send together.
    // Receive-> write to read buffer from fd
    // Send -> write to fd from send buffer.
    auto TCPSocket::sendAndRecv() noexcept -> bool {
        // ancillary data looks like 
        // [ cmsghdr header ][ padding ][ payload (struct timeval) ][ trailing padding ]
        // CMSG_SPACE() computes the amount of memory required to hold a payload of
        // timeval
        char ctrl[CMSG_SPACE(sizeof(struct timeval))];
        // ctrl start with message header.
        struct cmsghdr *cmsg = (struct cmsghdr *) &ctrl;

        struct iovec iov;
        // read into rcv_buffer + next valid index.
        iov.iov_base = rcv_buffer_ + next_rcv_valid_index_;
        // read buffer can hold upto TCPBufferSize bits.
        iov.iov_len = TCPBufferSize - next_rcv_valid_index_;

        msghdr msg;
        msg.msg_control = ctrl;
        msg.msg_controllen = sizeof(ctrl);
        // The sender. It was set to INADDR_ANY in connect.
        // TCP pins the other party at connect/accept.
        msg.msg_name = &inInAddr;
        msg.msg_namelen = sizeof(inInAddr);
        // points at the start of an array of iovec.
        msg.msg_iov = &iov;
        // number of separate buffer segments in msg_iov.
        // we have only 1 receive buffer.
        msg.msg_iovlen = 1;
        const auto n_rcv = recvmsg(fd_, &msg, MSG_DONTWAIT);
        if (n_rcv > 0) {
            next_rcv_valid_index_ += n_rcv;
            Nanos kernel_time = 0;
            // microsecond resolution in timeval.
            struct timeval time_kernel;
            if (cmsg->cmsg_level == SOL_SOCKET &&
                // SCM counterpart of SO_TIMESTAMP
                cmsg->cmsg_type == SCM_TIMESTAMP &&
                cmsg->cmsg_len == CMSG_LEN(sizeof(time_kernel))) {
                // CMSG_DATA extracts the payload from the message,
                // strips the header and paddings.
                memcpy(&time_kernel, CMSG_DATA(cmsg), sizeof(time_kernel));
                kernel_time = time_kernel.tv_sec * NANOS_TO_SECS + time_kernel.tv_usec * NANOS_TO_MICROS;
            }
            const auto user_time = getCurrentNanos();
            logger_.log("%:% %() % read socket:% len:% utime:% ktime:% diff:%\n",
                __FILE__, __LINE__,__FUNCTION__,
                Common::getCurrentNanos(),
                fd_, next_rcv_valid_index_, user_time,
                kernel_time, (user_time - kernel_time));
            recv_callback_(this, kernel_time);
        } else if (UNLIKELY(n_rcv == 0 || !wouldBlock())) {
            // 0: the peer closed the connection. An error other than no data: it's gone.
            recv_disconnected_ = true;
        }

        // Sends as much as the kernel takes, the rest stays in the buffer for the next call.
        size_t sent = 0;
        while (sent < next_send_valid_index_) {
            const auto n = ::send(fd_, send_buffer_ + sent, next_send_valid_index_ - sent, MSG_DONTWAIT | MSG_NOSIGNAL);
            if (UNLIKELY(n < 0)) {
                // Not a full kernel buffer: the connection is gone.
                if (!wouldBlock()) send_disconnected_ = true;
                break;
            }
            sent += n;
        }
        // ToDo - improvement
        // a slow peer's backlog is moved on every partial send, keep a start index instead.
        if (sent) {
            memmove(send_buffer_, send_buffer_ + sent, next_send_valid_index_ - sent);
            next_send_valid_index_ -= sent;
        }
        return (n_rcv > 0);
    }
}