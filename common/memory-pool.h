#pragma once
#include <cstdlib>
#include <new>
#include <stack>
#include <vector>
#include "macros.h"

namespace Common{
    // Fixed pool of N objects, allocate/deallocate are O(1).
    // Free slots are a stack of indices: deallocate pushes, allocate pops. The most recently
    // freed slot is reused first, it is likely still in cache.
    template<typename T, std::size_t N>
    class MemoryPool final{
        public:
            // All memory allocated and touched up front, nothing is allocated afterwards.
            MemoryPool():store_(N, T{}), free_(allIndices()){}

            MemoryPool(const MemoryPool&) = delete; // copy constructor can't be called.
            MemoryPool& operator=(const MemoryPool&) = delete; // copy assignment can't be called.
            MemoryPool(const MemoryPool&&) = delete; // move constructor can't be called.
            MemoryPool& operator=(const MemoryPool&&) = delete;// move assignment can't be called.

            template<typename ...Args>
            T* allocate(Args... args) noexcept {
                ASSERT(!free_.empty(), "There are no more free slots to allocate!");
                T* ret = &store_[free_.top()];
                free_.pop();
                return new(ret) T(args...);// new uses memory passed to it - ret in this case.
            }
            template<typename U>
            T* allocate(std::initializer_list<U> args) noexcept {
                ASSERT(!free_.empty(), "There are no more free slots to allocate!");
                T* ret = &store_[free_.top()];
                free_.pop();
                return new(ret) T(args);// new uses memory passed to it - ret in this case.
            }

            auto deallocate(const T* element) noexcept {
                const auto element_index = element - store_.data();
                ASSERT(element_index >= 0 && static_cast<std::size_t>(element_index) < N, "Element doesn't belong to this memory pool!");
                // More frees than allocations means a double free. It would also grow free_
                // past its capacity, a heap allocation.
                ASSERT(free_.size() < N, "Deallocated more elements than were allocated!");
                free_.push(static_cast<std::size_t>(element_index));
            }
        private:
            // Every slot free. Reversed so the first allocations hand out slots 0, 1, 2, ...
            static auto allIndices(){
                std::vector<std::size_t> indices(N);
                for(std::size_t i = 0; i < N; i++) indices[i] = N - 1 - i;
                return indices;
            }
            // On the heap: pools are large (the order pool is ~60 MB).
            std::vector<T> store_;
            std::stack<std::size_t, std::vector<std::size_t>> free_;
    };

}

