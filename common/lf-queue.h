#pragma once
#include "macros.h"
#include "memory-pool.h"
#include "constants.h"
#include <atomic>
#include <bit>
#include <mutex>
#include <condition_variable>

// Single producer, single consumer.
// Caveats:
// 1-> All LFQueues should be compile-time initialized.
// No runtime initialization, the vector initialization is from free memory.
// Capacity N is a compile time power of two, slot = counter & (N - 1).
// 2-> Can overwrite unread values if producer is fast enough to lap consumer.
// 3-> Reader blocks when there is nothing to read, returns nullptr only after stop().
namespace Common{
    template<typename T, size_t N>
    class LFQueue final{
        static_assert(std::has_single_bit(N), "LFQueue capacity must be a power of two.");
        // Constant mask: the slot is one `and` with an immediate, no division and no load.
        // Power of two also keeps slots in order when a counter wraps around at 2^64.
        static constexpr size_t MASK = N - 1;
        std::vector<T> store_;
        // Producer and consumer data on separate cache lines.
        alignas(CACHE_LINE_SIZE) size_t next_to_write_{0};
        mutable std::mutex write_access_;
        mutable std::condition_variable cv;
        // Set by stop() under write_access_, wakes the consumer for good.
        bool stopped_{false};
        alignas(CACHE_LINE_SIZE) size_t next_to_read_{0};
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
        // offset > 0 returns the slots after the next one, fill them and publish them all
        // with one updateWriteIndex(n).
        auto getNextWriteTo(size_t offset = 0) noexcept{
            return &store_[(next_to_write_ + offset) & MASK];
        }
        auto updateWriteIndex(size_t n = 1) noexcept{
            std::unique_lock<std::mutex> lock(write_access_);
            next_to_write_+= n;
            cv.notify_one();
        }

        // Consumer.
        // Blocks until there is data, returns nullptr only once stopped and fully drained.
        auto getNextReadTo() const noexcept -> const T*{
            if (next_to_read_ == cached_write_) {
                std::unique_lock<std::mutex> lock(write_access_);
                cv.wait(lock,[this](){return stopped_ || next_to_write_ != cached_write_;});
                cached_write_ = next_to_write_;
                if (next_to_read_ == cached_write_) return nullptr;
            }
            return &store_[next_to_read_ & MASK];
        }
        auto updateReadIndex() noexcept{
            ASSERT(next_to_read_ != cached_write_, "Read an invalid element.");
            next_to_read_++;
        }

        // Any thread. Elements published before stop() are still delivered.
        auto stop() noexcept{
            {
                std::lock_guard<std::mutex> lock(write_access_);
                stopped_ = true;
            }
            cv.notify_all();
        }
    };
}
// To Do - improvement
// use mutex + condition variable for synchronisation. skip busy wait.