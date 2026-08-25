#pragma once
#include <iostream>
#include <string>
#include <unordered_set>
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <fcntl.h>
#include "macros.h"
#include "logging.h"
#include "time-utils.h"
namespace Common {
  constexpr int MaxTCPServerBacklog = 1024;
  inline auto createSocket(Logger &logger, const std::string
    &t_ip, const std::string &iface, int port, bool is_udp,
    bool is_blocking, bool is_listening, int ttl, bool
    needs_so_timestamp) -> int;

  // gets the socket addr of network interface with name iface.
  inline auto getIfaceIP(const std::string &iface) -> std::string {
    char buf[NI_MAXHOST] = {'\0'};
    ifaddrs *ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != -1) {
      for (ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        // only IPv4.
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family ==
          AF_INET && iface == ifa->ifa_name) {
          getnameinfo(ifa->ifa_addr, sizeof(sockaddr_in),
            buf, sizeof(buf), NULL, 0, NI_NUMERICHOST);
          break;
        }
      }
      freeifaddrs(ifaddr);
    }
    return buf;
  }

  // sets the FD to be nonBlocking.
  // After each read, we need to check if the results is 
  // EWouldBlock or EAgain.
  inline auto setNonBlocking(int fd) -> bool {
    const auto flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
      return false;
    if (flags & O_NONBLOCK)
      return true;
    return (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1);
  }
  // Disable Nagle's Algorithm.
  inline auto setNoDelay(int fd) -> bool {
    int one = 1;
    return (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
      reinterpret_cast<void *>(&one), sizeof(one)) != -1);
  }
  // errno is thread-local
  // check it right after a syscall
  // before doing anything.
  inline auto wouldBlock() -> bool {
    // EWOULDBLOCK = EAGAIN
    return (errno == EWOULDBLOCK || errno == EINPROGRESS);
  }
  // Time To Live
  // How many Routers can a packet from this fd can visit
  // before being discarded.
  inline auto setTTL(int fd, int ttl) -> bool {
    return (setsockopt(fd, IPPROTO_IP, IP_TTL,
      reinterpret_cast<void *>(&ttl), sizeof(ttl)) != -1);
  }
  inline auto setMcastTTL(int fd, int mcast_ttl) noexcept -> bool {
    return (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL,
      reinterpret_cast<void *>(&mcast_ttl), sizeof
        (mcast_ttl)) != -1);
  }
  // Soft timeStamping.
  // Would use Hardware Timestamping if available/
  inline auto setSOTimestamp(int fd) -> bool {
    int one = 1; // setsockopt needs an address + a length.
    return (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMP,
      reinterpret_cast<void *>(&one), sizeof(one)) != -1);
  }

  inline auto createSocket(Logger &logger, const std::string&t_ip,
    const std::string &iface, int port,
    bool is_udp, bool is_blocking, bool
    is_listening, int ttl, bool
    needs_so_timestamp) -> int {
    std::string time_str;
    const auto ip = t_ip.empty() ? getIfaceIP(iface) : t_ip;
    logger.log("%:% %() % ip:% iface:% port:% is_udp:% is_blocking:% is_listening:% ttl:% SO_time:%\n",
        __FILE__, __LINE__, __FUNCTION__,
              Common::getCurrentTimeStr(&time_str), ip,
                iface, port, is_udp, is_blocking,
                  is_listening, ttl, needs_so_timestamp);
    addrinfo hints{};
    hints.ai_family = AF_INET; // IPv4 only.
    hints.ai_socktype = is_udp ? SOCK_DGRAM : SOCK_STREAM;
    hints.ai_protocol = is_udp ? IPPROTO_UDP : IPPROTO_TCP;
    hints.ai_flags = is_listening ? AI_PASSIVE : 0;
    if (std::isdigit(ip.c_str()[0])){hints.ai_flags |= AI_NUMERICHOST;}// Skip DNS if IP is already resolved.
    hints.ai_flags |= AI_NUMERICSERV;// Port is numeric. Skip service name lookup.
    addrinfo *result = nullptr;
    const auto rc = getaddrinfo(ip.c_str(), std::to_string(port).c_str(), &hints, &result);
    if (rc) {
      logger.log("getaddrinfo() failed. error:% errno:%\n",gai_strerror(rc), strerror(errno));
      return -1;
    }
    int fd = -1;
    int one = 1;
    for (addrinfo *rp = result; rp; rp = rp->ai_next) {
      fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if (fd == -1) {
        logger.log("socket() failed. errno:%\n",
          strerror(errno));
        return -1;
      }
    
      if (!is_blocking) {
        if (!setNonBlocking(fd)) {
          logger.log("setNonBlocking() failed. errno:%\n",strerror(errno));
          return -1;
        }
        if (!is_udp && !setNoDelay(fd)) {
          logger.log("setNoDelay() failed. errno:%\n",strerror(errno));
          return -1;
        }
      }
      if (needs_so_timestamp && !setSOTimestamp(fd)) {
        logger.log("setSOTimestamp() failed. errno:%\n", strerror(errno));
        return -1;
      }
      if (!is_listening && connect(fd, rp->ai_addr, rp->ai_addrlen) == -1 && !wouldBlock()) {
        logger.log("connect() failed. errno:%\n",
          strerror(errno));
        return -1;
      }
      constexpr int MaxTCPServerBacklog = 1024;
      // allows immediate bind on the IP + Port of this fd.
      // otherwise, restarts would have to wait to bind to the same IP + Port. 
      if (is_listening && setsockopt(fd, SOL_SOCKET,SO_REUSEADDR, reinterpret_cast<const char *>(&one),sizeof(one)) == -1) {
        logger.log("setsockopt() SO_REUSEADDR failed.errno:%\n", strerror(errno));
        return -1;
      }
      // bind the server.
      // can also bind client when the port is important.
      // otherwise kernel picks the port.
      if (is_listening && bind(fd, rp->ai_addr, rp->ai_addrlen) == -1) {
        logger.log("bind() failed. errno:%\n",
          strerror(errno));
        return -1;
      }
      // turn TCP State to LISTEN
      if (!is_udp && is_listening && listen(fd,MaxTCPServerBacklog) == -1) {
        logger.log("listen() failed. errno:%\n",
          strerror(errno));
        return -1;
      }
    }
    freeaddrinfo(result);
    return fd;
  }
}