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
// 2-> A full queue makes the producer wait until the consumer frees a slot (backpressure).
// Two threads that each write a queue the other reads can deadlock when both queues are full:
// the order server waits for the engine to read a request while the engine waits for the order
// server to read a response. Such a thread checks hasSpace() and keeps reading while it waits.
// 3-> Reader returns nullptr when there is nothing to read, forcing a nullcheck.
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
        // updates are plain stores, no atomic read-modify-write.
        // next_to_write_'s release store publishes the slots to the consumer, next_to_read_'s
        // release store hands read slots back to the producer. On x86 both are plain movs.
        // Producer and consumer data on separate cache lines, otherwise every write
        // invalidates the line the other thread is reading.
        std::atomic<size_t> next_to_write_{0};
        // Producer's last seen next_to_read_, reloaded only when the queue looks full, so
        // writing doesn't pull the consumer's cache line on every element.
        size_t cached_read_ = 0;
        // A/B experiment 2b: no alignas, next_to_read_ and cached_write_ share the producer's line
        // with next_to_write_ and cached_read_: 4 x 8 bytes from the line next_to_write_ starts.
        std::atomic<size_t> next_to_read_{0};
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
        //ToDo - research
        // Check memory_orders.

        // Producer.
        // True when the n slots after the published ones are free.
        auto hasSpace(size_t n = 1) noexcept -> bool{
            const auto end = next_to_write_.load(std::memory_order_relaxed) + n;
            if (end - cached_read_ <= N) return true;
            cached_read_ = next_to_read_.load(std::memory_order_acquire);
            return end - cached_read_ <= N;
        }
        // offset > 0 returns the slots after the next one, fill them and publish them all
        // with one updateWriteIndex(n).
        // Waits while the slot is unread. Reacting fast gains nothing then, the consumer sets the
        // pace, so pause between checks: fewer reloads of the consumer's cache line, which would
        // slow it down, and no syscall or sleep the producer has to wake up from.
        auto getNextWriteTo(size_t offset = 0) noexcept{
            while (UNLIKELY(!hasSpace(offset + 1))) __builtin_ia32_pause();
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
    };
}
