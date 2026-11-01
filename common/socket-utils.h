#pragma once
#include <iostream>
#include <sstream>
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
  struct SocketCfg {
    std::string ip_;
    std::string iface_;
    int port_ = -1;
    bool is_udp_ = false;
    bool is_listening_ = false;
    bool needs_so_timestamp_ =  false;

    auto toString() const {
      std::stringstream ss;
      ss << "SocketCfg[ip:" << ip_
      << " iface:" << iface_
      << " port:" << port_
      << " is_udp:" << is_udp_
      << " is_listening:" << is_listening_
      << " needs_SO_timestamp:" << needs_so_timestamp_
      << "]";

      return ss.str();
    }
  };
  constexpr int MaxTCPServerBacklog = 1024;

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
  // Multicast from fd goes out on iface, not on the interface the route to the group picks.
  // Set before connect(), connect() fixes the route.
  inline auto setMcastIface(int fd, const std::string &iface) -> bool {
    const in_addr addr{inet_addr(getIfaceIP(iface).c_str())};
    return (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &addr, sizeof(addr)) != -1);
  }
  // Subscribes fd to the multicast group ip on iface, the interface the publisher sends on.
  inline auto join(int fd, const std::string &ip, const std::string &iface) -> bool {
    const ip_mreq mreq{{inet_addr(ip.c_str())}, {inet_addr(getIfaceIP(iface).c_str())}};
    return (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != -1);
  }

  inline auto createSocket(Logger &logger, SocketCfg socket_cfg) -> int {
    std::string time_str;
    const std::string&t_ip{socket_cfg.ip_};
    const std::string &iface{socket_cfg.iface_};
    int port{socket_cfg.port_};
    bool is_udp{socket_cfg.is_udp_};
    bool is_listening{socket_cfg.is_listening_};
    bool needs_so_timestamp{socket_cfg.needs_so_timestamp_};
    const auto ip = t_ip.empty() ? getIfaceIP(iface) : t_ip;
    logger.log("%:% %() % ip:% cfg:%\n",
        __FILE__, __LINE__, __FUNCTION__,
              Common::getCurrentTimeStr(&time_str), ip, socket_cfg.toString());
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
    
      // Every socket is polled, none may block.
      if (!setNonBlocking(fd)) {
        logger.log("setNonBlocking() failed. errno:%\n",strerror(errno));
        return -1;
      }
      if (!is_udp && !setNoDelay(fd)) {
        logger.log("setNoDelay() failed. errno:%\n",strerror(errno));
        return -1;
      }
      // All clients are local: multicast is published on iface (lo), not over the network.
      if (is_udp && !is_listening && !setMcastIface(fd, iface)) {
        logger.log("setMcastIface() failed. errno:%\n", strerror(errno));
        return -1;
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