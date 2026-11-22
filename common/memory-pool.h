#pragma once
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>
#include <stack>
#include <type_traits>
#include <vector>
#include "macros.h"

namespace Common{
    // Fixed pool of N objects, allocate/deallocate are O(1).
    // Free slots are a stack of indices: deallocate pushes, allocate pops. The most recently
    // freed slot is reused first, it is likely still in cache.
    template<typename T, std::size_t N>
    class MemoryPool final{
        public:
            using value_type = T;
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

    // Raw storage for one object of up to Size bytes: the blocks a PoolAllocator hands out.
    template<std::size_t Size>
    struct alignas(std::max_align_t) PoolBlock{
        std::byte bytes[Size];
    };

    // Allocator for a node-based container (std::unordered_map, std::list, ...) that takes every
    // node from a MemoryPool of PoolBlocks. The pool's memory is allocated and touched up front, so
    // an insert never calls malloc and never touches a fresh page (a page fault, ~1 us).
    // The container rebinds the allocator to each type it allocates: its nodes, one at a time, and
    // for an unordered_map its bucket array, an array of pointers allocated once by reserve(),
    // which goes to std::allocator. Every rebound copy points at the same pool.
    template<typename T, typename Pool>
    class PoolAllocator{
        public:
            using value_type = T;

            explicit PoolAllocator(Pool* pool) noexcept : pool_(pool) {}
            template<typename U>
            PoolAllocator(const PoolAllocator<U, Pool>& other) noexcept : pool_(other.pool()) {}

            T* allocate(std::size_t n) noexcept {
                if constexpr (std::is_pointer_v<T>) {
                    return std::allocator<T>{}.allocate(n);
                } else {
                    using Block = typename Pool::value_type;
                    static_assert(sizeof(T) <= sizeof(Block) && alignof(T) <= alignof(Block),
                                  "The container's node doesn't fit in the pool's blocks.");
                    ASSERT(n == 1, "PoolAllocator hands out one node at a time!");
                    return reinterpret_cast<T*>(pool_->allocate());
                }
            }
            void deallocate(T* p, [[maybe_unused]] std::size_t n) noexcept {
                if constexpr (std::is_pointer_v<T>) {
                    std::allocator<T>{}.deallocate(p, n);
                } else {
                    pool_->deallocate(reinterpret_cast<typename Pool::value_type*>(p));
                }
            }
            auto pool() const noexcept { return pool_; }
        private:
            Pool* pool_;
    };
    // Equal when they share a pool: one can free what the other allocated.
    template<typename T, typename U, typename Pool>
    bool operator==(const PoolAllocator<T, Pool>& a, const PoolAllocator<U, Pool>& b) noexcept {
        return a.pool() == b.pool();
    }

}

