#pragma once
#include <functional>
#include <exchange/market-data/market-update.h>
#include <common/logging.h>
//#include "exchange/market_data/snapshot_synthesizer.h"

namespace Exchange {
    class MarketDataPublisher {
        public:
        MarketDataPublisher(MEMarketUpdateLFQueue *market_updates, const std::string &iface, const std::string &snapshot_ip, int snapshot_port, const std::string &incremental_ip, int incremental_port);
        auto start() noexcept -> void {
            run_ = true;
            ASSERT(Common::createAndStartThread(-1, "Exchange/MarketDataPublisher", [this]() { run(); }) != nullptr,
             "Failed to start MarketData thread.");
            snapshot_synthesizer_->start();
        }
        auto stop() noexcept -> void {
            run_ = false;
            snapshot_synthesizer_->stop();
        }
        ~MarketDataPublisher() {
            stop();
            using namespace std::literals::chrono_literals;
            std::this_thread::sleep_for(5s);
            delete snapshot_synthesizer_;
            snapshot_synthesizer_ = nullptr;
        }
        private:
        size_t next_inc_seq_num_ = 1;
        MEMarketUpdateLFQueue *outgoing_md_updates_{nullptr};
        MDPMarketUpdateLFQueue snapshot_md_updates_;
        std::atomic<bool> run_{false};
        std::string time_str_;
        Logger logger_;
        Common::McastSocket incremental_socket_;
        SnapshotSynthesizer *snapshot_synthesizer_ = nullptr;
        auto MarketDataPublisher::run() noexcept -> void;
    };
}