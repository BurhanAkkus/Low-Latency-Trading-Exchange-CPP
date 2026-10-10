#pragma once
#include "macros.h"
#include "memory-pool.h"
#include "constants.h"
#include <atomic>
#include <bit>
// To Do
// Carry this implementation to use memory-pool.

// Single producer, single consumer.
// Caveats:
// 1-> All LFQueues should be compile-time initialized.
// No runtime initialization, the vector initialization is from free memory.
// Capacity N is a compile time power of two, slot = counter & (N - 1).
// 2-> Can overwrite unread values if producer is fast enough to lap consumer.
// 3-> Reader returns nullptr when there is nothing to read..
namespace Common{
    template<typename T, size_t N>
    class LFQueue final{
        static_assert(std::has_single_bit(N), "LFQueue capacity must be a power of two.");
        // Constant mask: the slot is one `and` with an immediate, no division and no load.
        // Power of two also keeps slots in order when a counter wraps around at 2^64.
        static constexpr size_t MASK = N - 1;
        // On the heap: the log queue is 128 MB and queues are often local variables.
        std::vector<T> store_;
        // Counters only grow, slot = counter & MASK. Each is written by one thread only, so
        // updates are plain release stores, no atomic read-modify-write.
        // Producer and consumer data on separate cache lines, otherwise every write
        // invalidates the line the other thread is reading.
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> next_to_write_{0};
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> next_to_read_{0};
        // Consumer's last seen next_to_write_, reloaded only when the consumer catches up to it,
        // so draining a batch doesn't pull the producer's cache line on every read.
        mutable size_t cached_write_ = 0;
        public:
        LFQueue():store_(N,T{}){};
        // Delete other constructors;
        LFQueue(const LFQueue&) = delete; // Copy ctor
        LFQueue& operator=(const LFQueue&) = delete; // Copy assignment
        LFQueue(const LFQueue&&) = delete; // Move ctor
        LFQueue& operator=(const LFQueue&&) = delete; // Move assignment

        // Producer.
        // offset > 0 returns the slots after the next one, fill them and publish them all
        // with one updateWriteIndex(n).
        auto getNextWriteTo(size_t offset = 0) noexcept{
            return &store_[(next_to_write_.load(std::memory_order_relaxed) + offset) & MASK];
        }
        auto updateWriteIndex(size_t n = 1) noexcept{
            next_to_write_.store(next_to_write_.load(std::memory_order_relaxed) + n, std::memory_order_release);
        }

        // Consumer.
        auto getNextReadTo() const noexcept -> const T*{
            const auto read = next_to_read_.load(std::memory_order_relaxed);
            if (read == cached_write_) {
                cached_write_ = next_to_write_.load(std::memory_order_acquire);
                if (read == cached_write_) return nullptr;
            }
            return &store_[read & MASK];
        }
        auto updateReadIndex() noexcept{
            const auto read = next_to_read_.load(std::memory_order_relaxed);
            ASSERT(read != cached_write_, "Read an invalid element.");
            next_to_read_.store(read + 1, std::memory_order_release);
        }

        // Either thread.
        auto size() const noexcept{
            // Load read first: it never passes write, so the difference can't underflow.
            const auto read = next_to_read_.load(std::memory_order_acquire);
            return next_to_write_.load(std::memory_order_acquire) - read;
        }
    };
}
