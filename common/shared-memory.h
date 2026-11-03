#pragma once
#include "constants.h"
#include <atomic>
#include <bit>
#include <array>
#include <cstring>
#include <algorithm>
namespace Common{
    // A ring in shared memory with one writer and one reader, in two parts that can live apart:
    // its RingCounters and its RingBuffer<N>. The order server keeps all its clients' counters
    // next to each other, a pass over them doesn't touch the buffers (order-channels.h).
    // Ring<N, Unit> has the functions, the parts are their parameters: no pointers in shared
    // memory, each process maps it at another address.

    // The writer's line and the reader's line. Aligned to 128 bytes: the adjacent-line prefetcher
    // fetches lines in 128-byte pairs, in an array of these a pair is one ring's two lines.
    struct alignas(2 * CACHE_LINE_SIZE) RingCounters{
        // The writer's: how far it wrote, and next_to_read_ when it last loaded it.
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> next_to_write_{0};
        size_t cached_read_ = 0;
        // The reader's: how far it read. No cached next_to_write_: every reader reads all there
        // is, it would always say there's nothing left.
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> next_to_read_{0};
    };
    static_assert(sizeof(RingCounters) == 2 * CACHE_LINE_SIZE, "RingCounters is the writer's line and the reader's line.");
    static_assert(std::atomic<size_t>::is_always_lock_free, "The counters must work across processes.");

    template<size_t N>
    struct RingBuffer{
        static_assert(std::has_single_bit(N), "A ring's capacity must be a power of two.");
        alignas(CACHE_LINE_SIZE) std::array<char,N> memory_;
    };

    template<size_t N, size_t Unit = 1>
    struct Ring final{
        static constexpr size_t MASK = N - 1;

        // write as much as fits return how much was written. Loads the reader's next_to_read_
        // only when cached_read_ says there might not be room.
        static auto write(RingCounters& counters, RingBuffer<N>& buffer, const void* src, size_t len) noexcept -> size_t {
            auto written{counters.next_to_write_.load(std::memory_order_relaxed)};
            if(len > counters.cached_read_ + N - written){ counters.cached_read_ = counters.next_to_read_.load(std::memory_order_acquire);}
            len = std::min(len,counters.cached_read_ + N - written);
            if (len == 0){return len;}
            // write until end.
            auto until_end{N - (written & MASK)};
            memcpy(buffer.memory_.data() + (written & MASK), static_cast<const char*>(src), std::min(until_end, len));
            // wrap around to start.
            memcpy(buffer.memory_.data(), reinterpret_cast<const char*>(src) + std::min(until_end, len), len - std::min(until_end, len));
            counters.next_to_write_.store(written + len, std::memory_order_release);
            return len;
        };

        // read whole Units from read up to written, at most max bytes. return how much is read.
        // By value: read is the reader's next_to_read_, written the writer's next_to_write_ as
        // the caller loaded it (acquire: the bytes up to it are visible). The caller then stores
        // read + the result in next_to_read_ (release: the writer may write over them).
        static auto read(const RingBuffer<N>& buffer, size_t read, size_t written, void* dest, size_t max) noexcept -> size_t{
            max = std::min(max,written - read);
            max -= max % Unit;
            if(max == 0){return max;}
            // read until the end.
            memcpy(static_cast<char*>(dest), buffer.memory_.data() + (read & MASK), std::min(max, N - (read & MASK)));
            // wrap around to start.
            memcpy(static_cast<char*>(dest) + std::min(max, N - (read & MASK)), buffer.memory_.data(), max - std::min(max, N - (read & MASK)));
            return max;
        }
    };
}
