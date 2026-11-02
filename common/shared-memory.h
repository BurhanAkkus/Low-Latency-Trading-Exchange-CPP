#pragma once
#include "constants.h"
#include <atomic>
#include <bit>
#include <array>
#include <cstring>
#include <algorithm>
namespace Common{
    template<size_t N, size_t Unit = 1>
    class SharedMemory final{
        static_assert(std::has_single_bit(N), "SharedMemory capacity must be a power of two.");
        static constexpr size_t MASK = N - 1;
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> next_to_write_{0};
        size_t cached_read_ = 0;
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> next_to_read_{0};
        size_t cached_write_ = 0;

        alignas(CACHE_LINE_SIZE) std::array<char,N> memory_;
        public:
        SharedMemory() = default;

        // Delete other constructors;
        SharedMemory(const SharedMemory&) = delete; // Copy ctor
        SharedMemory& operator=(const SharedMemory&) = delete; // Copy assignment
        SharedMemory(const SharedMemory&&) = delete; // Move ctor
        SharedMemory& operator=(const SharedMemory&&) = delete; // Move assignment

        // write as much as fits return how much was written.
        auto write(const void* src, size_t len) noexcept -> size_t {
            auto written{next_to_write_.load(std::memory_order_relaxed)};
            auto until_end{N - (written & MASK)};
            if(len > cached_read_ + N - written){ cached_read_ = next_to_read_.load(std::memory_order_acquire);}
            len = std::min(len,cached_read_ + N - written);
            if (len == 0){return len;}
            // write until end.
            memcpy(memory_.data() + (written & MASK), static_cast<const char*>(src), std::min(until_end, len));
            // wrap around to start.
            memcpy(memory_.data(), reinterpret_cast<const char*>(src) + std::min(until_end, len), len - std::min(until_end, len));
            next_to_write_.store(written + len, std::memory_order_release);
            return len;
        };

        // read whatever can be read. return how much is read,
        auto read(void* dest, size_t max) noexcept -> size_t{
            auto read{next_to_read_.load(std::memory_order_relaxed)};
            if(max > cached_write_ - read){cached_write_ = next_to_write_.load(std::memory_order_acquire);}
            max = std::min(max,cached_write_ - read);
            max -= max % Unit;
            if(max == 0){return max;}
            // read until the end.
            memcpy(static_cast<char*>(dest), memory_.data() + (read & MASK), std::min(max, N - (read & MASK)));
            // wrap around to start.
            memcpy(static_cast<char*>(dest) + std::min(max, N - (read & MASK)), memory_.data(), max - std::min(max, N - (read & MASK)));
            next_to_read_.store(read + max, std::memory_order_release);
            return max;
        }
    };
}
