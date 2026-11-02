#pragma once
#include "common/logging.h"
#include "common/macros.h"
#include "common/shared-region.h"
#include "exchange/market-data/market-data-channel.h"
#include <memory>
#include <vector>

namespace Exchange{
    // The publishing end of the incremental market data over shared memory, in place of the
    // McastSocket, with the same send() and sendAndRecv(). init() creates the MarketDataChannel
    // region, send() copies an update into the pending buffer, sendAndRecv() moves what fits
    // into the ring and keeps the rest for the next call.
    class ShmMarketDataSocket{
        public:
        // Updates the reader's ring had no room for yet. A reader this far behind loses updates,
        // whole ones, as a lost datagram would: it sees the gap in the seq numbers.
        static constexpr size_t PENDING_BUFFER_SIZE = 1024 * 1024;

        explicit ShmMarketDataSocket(Logger& logger) : logger_(logger) {}

        // Delete defaults.
        ShmMarketDataSocket() = delete; // Default ctor
        ShmMarketDataSocket(const ShmMarketDataSocket&) = delete; // Copy ctor
        ShmMarketDataSocket& operator=(const ShmMarketDataSocket&) = delete; // Copy assignment
        ShmMarketDataSocket(const ShmMarketDataSocket&&) = delete; // Move ctor
        ShmMarketDataSocket& operator=(const ShmMarketDataSocket&&) = delete; // Move assignment

        // Creates the region.
        auto init() -> void {
            channel_ = std::make_unique<SharedRegion<MarketDataChannel>>(MARKET_DATA_CHANNEL_NAME, SharedRegion<MarketDataChannel>::Open::CREATE);
            logger_.log("%:% %() % shared memory % ready\n", __FILE__, __LINE__, __FUNCTION__,
                Common::getCurrentTimeStr(&time_str_), MARKET_DATA_CHANNEL_NAME);
        }

        // Into the pending buffer, a whole update or nothing.
        auto send(const void* data, size_t len) noexcept -> void {
            if (UNLIKELY(next_send_valid_index_ + len > PENDING_BUFFER_SIZE)) {
                ++dropped_;
                return;
            }
            memcpy(pending_.data() + next_send_valid_index_, data, len);
            next_send_valid_index_ += len;
        }

        // Moves the pending buffer into the ring, as much as fits. Nothing to receive on this end.
        auto sendAndRecv() noexcept -> void {
            if (!next_send_valid_index_) return;
            const auto sent = channel_->get()->incremental_.write(pending_.data(), next_send_valid_index_);
            if (sent) {
                memmove(pending_.data(), pending_.data() + sent, next_send_valid_index_ - sent);
                next_send_valid_index_ -= sent;
            }
        }

        // Updates dropped because the reader was too far behind.
        auto dropped() const noexcept { return dropped_; }

        private:
        Logger& logger_;
        std::string time_str_;
        std::unique_ptr<SharedRegion<MarketDataChannel>> channel_;
        // Zeroed: its pages are in before the first update.
        std::vector<char> pending_ = std::vector<char>(PENDING_BUFFER_SIZE);
        size_t next_send_valid_index_ = 0;
        size_t dropped_ = 0;
    };
}
