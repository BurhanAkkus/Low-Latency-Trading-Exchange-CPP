#pragma once
#include <cstdlib>
#include <vector>
#include <stack>
#include "macros.h"

namespace Common{
    template<typename T>
    class MemoryPool final{
        public:
            MemoryPool(std::size_t num_elems):store(num_elems,{T{},true}){
                // Make sure that the ObjectBlock's memory starts with object_.
                ASSERT(reinterpret_cast<ObjectBlock*>(&(store[0].object_)) == &(store[0]), "ObjectBlock must start with T");
            }

            MemoryPool() = delete; // default constructor can't be called.
            MemoryPool(const MemoryPool&) = delete; // copy constructor can't be called.
            MemoryPool& operator=(const MemoryPool&) = delete; // copy assignment can't be called.
            MemoryPool(const MemoryPool&&) = delete; // move constructor can't be called.
            MemoryPool& operator=(const MemoryPool&&) = delete;// move assignment can't be called.

            template<typename ...Args>
            T* allocate(Args... args) noexcept {
                ASSERT(!frees_.empty(),"There are no more free slots to allocate!");
                auto next_free_obj = &store[frees_.top()];
                T* ret = &next_free_obj;
                ret = new(ret) T(args...);// new uses memory passed to it - ret in this case.
                frees_.pop();
                return ret;
            }
            template<typename U>
            T* allocate(std::initializer_list<U> args) noexcept {
                ASSERT(!frees_.empty(),"There are no more free slots to allocate!")
                auto next_free_obj = &store[frees_.top()];
                T* ret = &next_free_obj;
                ret = new(ret) T(args);// new uses memory passed to it - ret in this case.
                frees_.pop();
                return ret;
            }

            auto deallocate(const T* element) noexcept {
                const auto element_index = element - &store[0];
                ASSERT(element_index >= 0 && static_cast<size_t>(element_index) < store.size(),"Element doesn't belong to this memory pool!" );
                frees_.push(element_index);
            }
        private:
            std::vector<T> store;
            std::stack<size_t> frees_;
    }; 

}