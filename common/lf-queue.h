#pragma once
#include "macros.h"
#include "memory-pool.h"
#include <atomic>
// To Do
// Carry this implementation to use memory-pool.

// Caveats:
// 1-> All LFQueues should be compile-time initialized.
// No runtime initialization, the vector initialization is from free memory.
// 2-> Can overwrite unread values if producer is fast enough to lap consumer.
// 3-> Reader returns nullptr when there is nothing to read..
namespace Common{
    template<typename T>
    class LFQueue final{
        std::vector<T> store_;
        std::atomic<size_t> next_to_write_{0};
        std::atomic<size_t> next_to_read_{0};
        std::atomic<size_t> size_{0};
        public:
        LFQueue(size_t num_elems):store_(num_elems,T{}){};
        // Delete other constructors;
        LFQueue() = delete; // Default ctor
        LFQueue(const LFQueue&) = delete; // Copy ctor
        LFQueue& operator=(const LFQueue&) = delete; // Copy assignment
        LFQueue(const LFQueue&&) = delete; // Move ctor
        LFQueue& operator=(const LFQueue&&) = delete; // Move assignment
        
        auto getNextWriteTo() noexcept{
            return &store_[next_to_write_];
        }
        auto updateWriteIndex() noexcept{
            next_to_write_ = (next_to_write_ + 1) % store_.size();
            size_++;
        }
        
        auto getNextReadTo() const noexcept -> const T*{
            return size_.load() ? &store_[next_to_read_] : nullptr;
        }
        auto updateReadIndex() noexcept{
            next_to_read_ = (next_to_read_ + 1) % store_.size();
            ASSERT(size_ != 0, "Read an invalid element in: " + std::to_string(pthread_self()));
            size_--;
        }

        auto size() const noexcept{
            return size_.load();
        }
    };
}