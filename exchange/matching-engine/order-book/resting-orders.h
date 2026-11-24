#pragma once
#include "common/types.h"
#include "common/constants.h"
#include "common/memory-pool.h"
#include "exchange/matching-engine/matcher/me-order.h"
#include <array>
#include <cstddef>
#include <cstdint>
using namespace Common;

namespace Exchange{
    // A book's resting orders by client id and client order id: one long row of places, read as
    // buckets of 10. An order's ids give its start bucket, and it takes the first free place from
    // there on.
    class RestingOrders{
        public:
            using OrderPool = MemoryPool<MEOrder, ME_MAX_RESTING_ORDERS>;

            // The pool of the book's orders.
            explicit RestingOrders(OrderPool& order_pool) noexcept : order_pool_(order_pool) {}

            // The resting order with these ids, nullptr if there is none. Looks where insert() could
            // have put it: from its start bucket on, up to a bucket no order went past.
            inline auto find(ClientId client_id, OrderId client_order_id) const noexcept -> MEOrder*{
                const auto start = startBucket(client_id, client_order_id);
                auto b = start;
                do{
                    const auto& bucket = buckets_[b];
                    for(const auto& element : bucket.elements){
                        if(element.client_order_id == client_order_id && element.client_id == client_id && element.used)
                            return order_pool_.at(element.order_index);
                    }
                    if(bucket.overflow == 0) return nullptr;
                    b = nextBucket(b);
                }while(b != start);
                return nullptr;
            }
            // Takes the first free place from the order's start bucket on. Every full bucket on the
            // way counts the order in its overflow. The order keeps its place, for erase().
            inline auto insert(MEOrder* order) noexcept -> void{
                for(auto b = startBucket(order->client_id_, order->client_order_id_); ; b = nextBucket(b)){
                    auto& bucket = buckets_[b];
                    for(std::size_t i = 0; i < BUCKET_SIZE; i++){
                        auto& element = bucket.elements[i];
                        if(element.used) continue;
                        element.client_order_id = static_cast<uint16_t>(order->client_order_id_);
                        element.client_id = static_cast<uint8_t>(order->client_id_);
                        element.used = true;
                        element.order_index = static_cast<uint16_t>(order_pool_.indexOf(order));
                        order->resting_place_ = static_cast<uint16_t>(b * BUCKET_SIZE + i);
                        return;
                    }
                    bucket.overflow++;
                }
            }
            // Frees the order's place, and takes the order out of the overflow of every bucket it
            // went past.
            inline auto erase(const MEOrder* order) noexcept -> void{
                const std::size_t own = order->resting_place_ / BUCKET_SIZE;
                buckets_[own].elements[order->resting_place_ % BUCKET_SIZE].used = false;
                for(auto b = startBucket(order->client_id_, order->client_order_id_); b != own; b = nextBucket(b))
                    buckets_[b].overflow--;
            }
        private:
            // To experiment with: 64 bytes each per book. The fuller the row, the further an order
            // walks from its start bucket.
            static constexpr std::size_t BUCKETS = 2048;

            // One place.
            struct Element{
                uint16_t client_order_id = 0;
                uint8_t client_id = 0;
                bool used = false;
                uint16_t order_index = 0;   // in the book's order pool
            };
            static_assert(sizeof(Element) == 6);
            static_assert(ME_MAX_ORDER_PER_CLIENT <= 65536, "Client order ids don't fit an element.");
            static_assert(ME_MAX_NUM_CLIENTS <= 256, "Client ids don't fit an element.");
            static_assert(ME_MAX_RESTING_ORDERS <= 65536, "Order indices don't fit an element.");

            // One cache line.
            static constexpr std::size_t BUCKET_SIZE = 10;
            struct alignas(64) Bucket{
                std::array<Element, BUCKET_SIZE> elements;
                uint16_t overflow = 0;      // orders that went on to a later bucket because this one was full
            };
            static_assert(sizeof(Bucket) == 64);

            // Place p is element p % BUCKET_SIZE of bucket p / BUCKET_SIZE.
            static constexpr std::size_t PLACES = BUCKETS * BUCKET_SIZE;
            static_assert(PLACES >= ME_MAX_RESTING_ORDERS, "Every order needs a place.");
            static_assert(PLACES <= 65536, "Place numbers don't fit 16 bits.");

            // Every client has its own start bucket, and its order ids count up from there. BUCKETS /
            // the golden ratio, made odd, spaces the clients evenly.
            static constexpr std::size_t CLIENT_STEP = static_cast<std::size_t>(BUCKETS / 1.6180339887) | 1;
            static auto startBucket(ClientId client_id, OrderId client_order_id) noexcept -> std::size_t{
                return (client_order_id + client_id * CLIENT_STEP) % BUCKETS;
            }
            // The row is a ring: the first bucket follows the last.
            static auto nextBucket(std::size_t bucket) noexcept -> std::size_t{
                return (bucket + 1) % BUCKETS;
            }

            OrderPool& order_pool_;
            std::array<Bucket, BUCKETS> buckets_{};
    };
}
