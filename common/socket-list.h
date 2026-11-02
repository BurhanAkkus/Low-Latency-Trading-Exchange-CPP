#pragma once
#include "constants.h"
#include <array>
namespace Common {
  // Sockets in a list: contains, insert, erase and clear in O(1), iterated like an array. Each
  // socket keeps its place in the list in Index, one field per list: a socket can be in several.
  // Every slot below size_ holds a socket in the list, so a socket is in it when the slot its
  // Index points to holds it. A socket that left keeps a stale Index, which fails that check.
  // Erase moves the last socket into the gap, so the order changes. Don't erase while
  // iterating the same list, keepIf() removes while it iterates.
  template <typename Socket, size_t Socket::*Index>
  struct SocketList {
    std::array<Socket *, ME_MAX_NUM_CLIENTS> sockets_{};
    size_t size_ = 0;
    auto contains(const Socket *socket) const noexcept {
      // Unsigned: index < size_ also keeps sockets_[index] in bounds.
      const size_t index = socket->*Index;
      return index < size_ && sockets_[index] == socket;
    }
    auto insert(Socket *socket) noexcept -> void {
      if (contains(socket)) return;
      push(socket);
    }
    // Insert without the contains check: the caller guarantees the socket isn't in the list.
    auto push(Socket *socket) noexcept -> void {
      socket->*Index = size_;
      sockets_[size_++] = socket;
    }
    // The last socket takes the erased one's place.
    auto erase(Socket *socket) noexcept -> void {
      if (!contains(socket)) return;
      Socket *last = sockets_[--size_];
      sockets_[socket->*Index] = last;
      last->*Index = socket->*Index;
    }
    // Every socket leaves the list.
    auto clear() noexcept -> void { size_ = 0; }
    // Keeps the sockets keep() returns true for, in their order, the others leave the list.
    // keep() must not change the list.
    template <typename Keep>
    auto keepIf(Keep keep) noexcept -> void {
      size_t kept = 0;
      for (size_t i = 0; i < size_; ++i) {
        auto socket = sockets_[i];
        if (keep(socket)) { socket->*Index = kept; sockets_[kept++] = socket; }
      }
      size_ = kept;
    }
    auto begin() const noexcept { return sockets_.begin(); }
    auto end() const noexcept { return sockets_.begin() + size_; }
  };
}
