#include "mcast-socket.h"

namespace Common {
  /// Initialize multicast socket to read from or publish to a stream.
  /// Does not join the multicast stream yet.
  auto McastSocket::init(const std::string &ip, const std::string &iface, int port, bool is_listening) -> int {
    const SocketCfg socket_cfg{ip, iface, port, true, is_listening, false};
    iface_ = iface;
    socket_fd_ = createSocket(logger_, socket_cfg);
    return socket_fd_;
  }

  /// Add / Join membership / subscription to a multicast stream, on the interface from init().
  bool McastSocket::join(const std::string &ip) {
    return Common::join(socket_fd_, ip, iface_);
  }

  /// Remove / Leave membership / subscription to a multicast stream.
  auto McastSocket::leave(const std::string &, int) -> void {
    close(socket_fd_);
    socket_fd_ = -1;
  }

  /// Publish outgoing data and read incoming data.
  auto McastSocket::sendAndRecv() noexcept -> bool {
    // Read data and dispatch callbacks if data is available - non blocking.
    const ssize_t n_rcv = recv(socket_fd_, inbound_data_.data() + next_rcv_valid_index_, McastBufferSize - next_rcv_valid_index_, MSG_DONTWAIT);
    if (n_rcv > 0) {
      next_rcv_valid_index_ += n_rcv;
      logger_.log("%:% %() % read socket:% len:%\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_), socket_fd_,
                  next_rcv_valid_index_);
      recv_callback_(this);
    }

    // Publish market data in the send buffer to the multicast stream.
    flush();

    return (n_rcv > 0);
  }

  /// Copy one whole message to the datagram being built - does not send it out yet,
  /// unless the message doesn't fit, then the datagram so far is sent first.
  /// A message never spans two datagrams, so a lost datagram loses whole messages only.
  auto McastSocket::send(const void *data, size_t len) noexcept -> void {
    ASSERT(len <= McastMaxDatagramSize, "Message larger than one datagram.");
    if (next_send_valid_index_ + len > McastMaxDatagramSize) flush();
    memcpy(outbound_data_.data() + next_send_valid_index_, data, len);
    next_send_valid_index_ += len;
  }

  /// Send the datagram built so far.
  /// A failed send drops the datagram, receivers see the gap in the seq numbers and recover
  /// from the snapshot stream.
  auto McastSocket::flush() noexcept -> void {
    if (next_send_valid_index_ == 0) return;
    const ssize_t n = ::send(socket_fd_, outbound_data_.data(), next_send_valid_index_, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (UNLIKELY(n < 0))
      logger_.log("%:% %() % send failed socket:% len:% error:%\n", __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), socket_fd_, next_send_valid_index_, strerror(errno));
    else
      logger_.log("%:% %() % send socket:% len:%\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_), socket_fd_, n);
    next_send_valid_index_ = 0;
  }
}