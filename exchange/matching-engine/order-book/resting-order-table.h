#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include "common/constants.h"
#include "common/macros.h"
#include "common/memory-pool.h"
#include "common/types.h"
#include "exchange/matching-engine/matcher/me-order.h"
using namespace Common;

namespace Exchange{
    // The book's resting orders by (client id, client order id). Open addressing with buckets of one
    // cache line: a find reads one line, an insert takes a free slot in it, an erase clears one tag
    // at the location the order keeps. Nothing is allocated after construction.
    //
    // The key, client_order_id << 8 | client_id, is 24 bits (see CLIENT_BITS and ORDER_ID_BITS). An
    // odd multiply shuffles the 24-bit keys 1 to 1; the shuffled key's top 11 bits pick the home
    // bucket and the other 13 go into the slot's tag. A bucket and a tag give back exactly one key,
    // so a tag match is a key match, there is nothing to check in the order.
    //
    // A full bucket sends an insert on to the next one. The tag's low 3 bits hold the distance from
    // the home bucket + 1 (0 is an empty slot), so an entry that spilled can't be taken for one of
    // the bucket it sits in. Every full bucket an insert passes counts it in its overflow; a find
    // goes on to the next bucket only while that count isn't 0, so a find for a key that isn't
    // there usually reads one line.
    class RestingOrderTable final{
        public:
            using OrderPool = MemoryPool<MEOrder, ME_MAX_RESTING_ORDERS>;

            explicit RestingOrderTable(OrderPool* orders) noexcept : orders_(orders) {}
            RestingOrderTable(const RestingOrderTable&) = delete;
            RestingOrderTable& operator=(const RestingOrderTable&) = delete;
            RestingOrderTable(RestingOrderTable&&) = delete;
            RestingOrderTable& operator=(RestingOrderTable&&) = delete;

            // The resting order with this key, nullptr if there is none.
            auto find(ClientId client_id, OrderId client_order_id) noexcept -> MEOrder*{
                const auto key = shuffle(client_id, client_order_id);
                const auto home = key >> REMAINDER_BITS;
                const auto remainder = (key & REMAINDER_MASK) << DISTANCE_BITS;
                for(uint32_t distance = 0; distance <= MAX_DISTANCE; distance++){
                    const Bucket& bucket = buckets_[(home + distance) & BUCKET_MASK];
                    const auto slots = matching(bucket, static_cast<uint16_t>(remainder | (distance + 1)));
                    if(slots) return orders_->at(bucket.orders[std::countr_zero(slots)]);
                    if(LIKELY(bucket.overflow == 0)) return nullptr;
                }
                return nullptr;
            }

            // Adds a resting order, which must not be in the table yet, and stores its location in it.
            auto insert(MEOrder* order) noexcept -> void{
                const auto key = shuffle(order->client_id_, order->client_order_id_);
                const auto home = key >> REMAINDER_BITS;
                const auto remainder = (key & REMAINDER_MASK) << DISTANCE_BITS;
                for(uint32_t distance = 0; distance <= MAX_DISTANCE; distance++){
                    const auto index = (home + distance) & BUCKET_MASK;
                    Bucket& bucket = buckets_[index];
                    const auto free_slots = matching(bucket, EMPTY);
                    if(LIKELY(free_slots)){
                        const auto slot = static_cast<uint32_t>(std::countr_zero(free_slots));
                        bucket.tags[slot] = static_cast<uint16_t>(remainder | (distance + 1));
                        bucket.orders[slot] = static_cast<uint16_t>(orders_->indexOf(order));
                        order->resting_slot_ = static_cast<uint16_t>(index << SLOT_BITS | slot);
                        // The full buckets it passed: finds for their keys now go on past them.
                        for(uint32_t passed = 0; passed < distance; passed++){
                            buckets_[(home + passed) & BUCKET_MASK].overflow++;
                        }
                        return;
                    }
                }
                FATAL("RestingOrderTable: no free slot within MAX_DISTANCE buckets of the home bucket!");
            }

            // Removes the order at the location its insert() stored in it.
            auto erase(const MEOrder* order) noexcept -> void{
                const uint32_t index = order->resting_slot_ >> SLOT_BITS;
                const uint32_t slot = order->resting_slot_ & SLOT_MASK;
                Bucket& bucket = buckets_[index];
                const uint32_t distance = (bucket.tags[slot] & DISTANCE_MASK) - 1u;
                bucket.tags[slot] = EMPTY;
                // The full buckets its insert passed.
                for(uint32_t passed = 1; passed <= distance; passed++){
                    buckets_[(index - passed) & BUCKET_MASK].overflow--;
                }
            }

        private:
            // The key's parts. Larger ids would make two keys share a bucket and tag, a wrong match.
            static constexpr uint32_t CLIENT_BITS = 8;
            static constexpr uint32_t ORDER_ID_BITS = 16;
            static_assert(ME_MAX_NUM_CLIENTS <= (1u << CLIENT_BITS), "Client ids don't fit the key.");
            static_assert(ME_MAX_ORDER_PER_CLIENT <= (1u << ORDER_ID_BITS), "Client order ids don't fit the key.");
            static constexpr uint32_t KEY_BITS = CLIENT_BITS + ORDER_ID_BITS;          // 24
            static constexpr uint32_t KEY_MASK = (1u << KEY_BITS) - 1;
            // 2,048 buckets, the shuffled key's top 11 bits.
            static constexpr uint32_t BUCKET_BITS = 11;
            static constexpr uint32_t BUCKET_MASK = (1u << BUCKET_BITS) - 1;
            // The rest of the key goes into the 16-bit tag, the tag's other bits hold the distance.
            static constexpr uint32_t REMAINDER_BITS = KEY_BITS - BUCKET_BITS;        // 13
            static constexpr uint32_t REMAINDER_MASK = (1u << REMAINDER_BITS) - 1;
            static constexpr uint32_t DISTANCE_BITS = 16 - REMAINDER_BITS;            // 3
            static constexpr uint32_t DISTANCE_MASK = (1u << DISTANCE_BITS) - 1;
            // Distance + 1 is stored, 0 stays free for an empty slot: 0 to 6 buckets from home.
            static constexpr uint32_t MAX_DISTANCE = DISTANCE_MASK - 1;
            static constexpr uint16_t EMPTY = 0;
            // 15 slots, the order's location is bucket << 4 | slot.
            static constexpr uint32_t SLOTS = 15;
            static constexpr uint32_t SLOT_BITS = 4;
            static constexpr uint32_t SLOT_MASK = (1u << SLOT_BITS) - 1;
            static_assert(SLOTS <= SLOT_MASK + 1 && BUCKET_BITS + SLOT_BITS <= 16, "A location doesn't fit 16 bits.");
            static_assert(ME_MAX_RESTING_ORDERS <= (1u << 16), "Order indices don't fit 16 bits.");
            // 30,720 slots for 16,384 orders: a full book leaves the table ~53% full.
            static_assert((1u << BUCKET_BITS) * SLOTS >= ME_MAX_RESTING_ORDERS * 3 / 2, "Too few slots.");
            // 2^24 / the golden ratio, odd: multiplying by it shuffles the 24-bit keys 1 to 1 and
            // spreads a client's consecutive order ids over all buckets.
            static constexpr uint32_t SHUFFLE = 0x9E3779;
            static_assert(SHUFFLE & 1, "Only an odd multiplier shuffles 1 to 1.");

            // One cache line.
            struct alignas(64) Bucket{
                uint16_t tags[SLOTS];      // the key's remainder << 3 | distance + 1, 0 = empty
                uint16_t orders[SLOTS];    // the order's index in the order pool
                uint16_t overflow;         // inserts that found this bucket full and went on
            };
            static_assert(sizeof(Bucket) == 64, "A bucket is one cache line.");

            static auto shuffle(ClientId client_id, OrderId client_order_id) noexcept -> uint32_t{
                const auto key = static_cast<uint32_t>(client_order_id << CLIENT_BITS | client_id);
                return (key * SHUFFLE) & KEY_MASK;
            }
            // Bit i set for every slot whose tag is tag.
            static auto matching(const Bucket& bucket, uint16_t tag) noexcept -> uint32_t{
                uint32_t slots = 0;
                for(uint32_t i = 0; i < SLOTS; i++){
                    slots |= static_cast<uint32_t>(bucket.tags[i] == tag) << i;
                }
                return slots;
            }

            // Zeroed (all slots empty) and so touched in the constructor, 128 KB.
            std::array<Bucket, 1u << BUCKET_BITS> buckets_{};
            OrderPool* orders_;
    };
}
