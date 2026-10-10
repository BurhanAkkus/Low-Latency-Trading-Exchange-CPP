#pragma once
#include <iostream>
#include <cstring>
#define LIKELY(x) __builtin_expect(!!(x), 1) // [[likely]]
#define UNLIKELY(x) __builtin_expect(!!(x), 0)// [[unlikely]]

inline auto ASSERT(bool cond, const std::string& msg)
  noexcept {
  if(UNLIKELY(!cond)) {
    std::cerr << msg << std::endl;
    exit(EXIT_FAILURE);
  }
}
// String literal messages, avoids building a std::string on every call.
inline auto ASSERT(bool cond, const char* msg)
  noexcept {
  if(UNLIKELY(!cond)) {
    std::cerr << msg << std::endl;
    exit(EXIT_FAILURE);
  }
}
inline auto FATAL(const std::string& msg) noexcept {
  std::cerr << msg << std::endl;
  exit(EXIT_FAILURE);
}

