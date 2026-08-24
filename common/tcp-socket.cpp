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
        if (len > 0) {
        memcpy(send_buffer_ + next_send_valid_index_, data, len);
        next_send_valid_index_ += len;
        }
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
                Common::getCurrentTimeStr(&time_str_),
                fd_, next_rcv_valid_index_, user_time,
                kernel_time, (user_time - kernel_time));
            recv_callback_(this, kernel_time);
        }

        // SEND
        // defensive cap of TCPBufferSize.
        ssize_t n_send = std::min(TCPBufferSize, next_send_valid_index_);
        // There is something to send in send buffer.
        //ToDo
        // Handle new data being pushed to send buffer while inside the loop.
        while (n_send > 0) {
            // defensive cap of next_send_valid_index_, should never bind.
            auto n_send_this_msg = std::min(static_cast<ssize_t>(next_send_valid_index_),
             n_send);
            // Non-blocking send, Supress SigPipe if peer is dced.
            const int flags = MSG_DONTWAIT | MSG_NOSIGNAL | 
                (n_send_this_msg < n_send ? MSG_MORE : 0);
            // n bytes sent. Read from the beginning of send_buffer.
            auto n = ::send(fd_, send_buffer_, n_send_this_msg,
                flags);
            // error during send
            if (UNLIKELY(n < 0)) {
                // error is not due to NotBlocking.
                if (!wouldBlock())
                    send_disconnected_ = true;
                // ToDo
                // breaks even if wouldBlock is true and a retry could remedy.
                // Add Retry.
                break;
            }
            logger_.log("%:% %() % send socket:% len:%\n",
                __FILE__, __LINE__, __FUNCTION__,
                Common::getCurrentTimeStr(&time_str_), fd_, n);
            // remaining bytes to send after n bytes are sent.
            n_send -= n;
            ASSERT(n == n_send_this_msg, "Don't support partial send lengths yet.");
        }
        next_send_valid_index_ = 0;
        return (n_rcv > 0);
    }
}