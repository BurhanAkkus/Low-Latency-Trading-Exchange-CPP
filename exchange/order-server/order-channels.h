#pragma once
#include "common/constants.h"
#include "common/shared-memory.h"
#include "exchange/order-server/client-request.h"
#include "exchange/order-server/client-response.h"
#include <array>
#include <atomic>

namespace Exchange{
    // The order server's shared memory transport, in place of its TCP connections: the exchange
    // creates it (common/shared-region.h), the benchmark's clients attach. One slot per client id:
    // the client's requests in one ring, the order server's responses to it in the other. Each
    // ring has one writer and one reader, a read returns whole messages only.
    constexpr const char* ORDER_CHANNELS_NAME = "/exchange-orders";
    constexpr size_t ORDER_RING_SIZE = 64 * 1024;

    // A slot's client, in place of TCP's accept and end of stream: the client sets CONNECTED on a
    // FREE slot and CLOSED when it's done, counting each in changes_. The order server sets CLOSED
    // when it drops a client itself. A CLOSED slot isn't used again until the next run (a new region).
    enum class ClientState : uint32_t{
        FREE = 0,
        CONNECTED = 1,
        CLOSED = 2
    };

    struct OrderChannels{
        // Counts every CONNECTED and CLOSED a client sets: the order server rescans the slots'
        // states only when it changed.
        alignas(CACHE_LINE_SIZE) std::atomic<uint32_t> changes_{0};
        struct Client{
            alignas(CACHE_LINE_SIZE) std::atomic<ClientState> state_{ClientState::FREE};
            // Client -> order server.
            SharedMemory<ORDER_RING_SIZE, sizeof(OMClientRequest)> requests_;
            // Order server -> client: the sequence number and the response, one OMClientResponse.
            SharedMemory<ORDER_RING_SIZE, sizeof(OMClientResponse)> responses_;
        };
        static_assert(std::atomic<ClientState>::is_always_lock_free, "The state must work across processes.");
        std::array<Client, ME_MAX_NUM_CLIENTS> clients_;
    };
}
