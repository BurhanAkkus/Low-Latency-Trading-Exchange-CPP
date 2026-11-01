#pragma once
#include <functional>
#include <exchange/market-data/market-update.h>
#include <common/logging.h>
#include "exchange/market-data/snapshot-synthesizer.h"

namespace Exchange {
    class MarketDataPublisher {
        public:
        MarketDataPublisher(MEMarketUpdateLFQueue *market_updates, const std::string &iface, const std::string &snapshot_ip, int snapshot_port, const std::string &incremental_ip, int incremental_port);
        // core ids -1 leave the threads unpinned.
        auto start(int core_id = -1, int snapshot_core_id = -1) noexcept -> void {
            run_ = true;
            thread_ = Common::createAndStartThread(core_id, "Exchange/MarketDataPublisher", [this]() { run(); });
            ASSERT(thread_ != nullptr, "Failed to start MarketData thread.");
            snapshot_synthesizer_->start(snapshot_core_id);
        }
        // Stops the publisher only, the synthesizer stops when it is deleted.
        auto stop() noexcept -> void {
            run_ = false;
        }
        ~MarketDataPublisher() {
            stop();
            // The publisher can be waiting for space in the snapshot queue: the synthesizer
            // keeps reading it until the publisher is done, then stops.
            if (thread_) { thread_->join(); delete thread_; thread_ = nullptr; }
            delete snapshot_synthesizer_;
            snapshot_synthesizer_ = nullptr;
        }
        private:
        std::thread* thread_ = nullptr;
        size_t next_inc_seq_num_ = 1;
        MEMarketUpdateLFQueue *outgoing_md_updates_{nullptr};
        MDPMarketUpdateLFQueue snapshot_md_updates_;
        std::atomic<bool> run_{false};
        std::string time_str_;
        Logger logger_;
        Common::McastSocket incremental_socket_;
        SnapshotSynthesizer *snapshot_synthesizer_ = nullptr;
        auto run() noexcept -> void;
    };
}