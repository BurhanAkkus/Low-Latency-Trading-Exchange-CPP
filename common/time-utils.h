#pragma once
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
namespace Common {
  using Nanos =  int64_t;
  constexpr Nanos NANOS_TO_MICROS = 1000;
  constexpr Nanos MICROS_TO_MILLIS = 1000;
  constexpr Nanos MILLIS_TO_SECS = 1000;
  constexpr Nanos NANOS_TO_MILLIS = NANOS_TO_MICROS *
    MICROS_TO_MILLIS;
  constexpr Nanos NANOS_TO_SECS = NANOS_TO_MILLIS *
    MILLIS_TO_SECS;
  constexpr Nanos SECS_PER_MINUTE = 60;
  constexpr Nanos SECS_PER_HOUR = 60 * SECS_PER_MINUTE;
  constexpr Nanos SECS_PER_DAY = 24 * SECS_PER_HOUR;
  inline auto getCurrentNanos() noexcept {
    return std::chrono::duration_cast
      <std::chrono::nanoseconds>(std::chrono::
        system_clock::now().time_since_epoch()).count();
  }

  // UTC "HH:MM:SS.nnnnnnnnn"
  // not to be used in the hot path.
  inline auto& getCurrentTimeStr(std::string* time_str) {
    const auto nanos_since_epoch = getCurrentNanos();
    const auto secs_of_day = (nanos_since_epoch / NANOS_TO_SECS) % SECS_PER_DAY;
    const auto nanos = nanos_since_epoch % NANOS_TO_SECS;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02ld:%02ld:%02ld.%09ld",
      static_cast<long>(secs_of_day / SECS_PER_HOUR),
      static_cast<long>(secs_of_day % SECS_PER_HOUR / SECS_PER_MINUTE),
      static_cast<long>(secs_of_day % SECS_PER_MINUTE),
      static_cast<long>(nanos));
    time_str->assign(buf);
    return *time_str;
  }
}
