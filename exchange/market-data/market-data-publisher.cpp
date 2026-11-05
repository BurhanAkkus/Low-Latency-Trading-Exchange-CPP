#include "exchange/market-data/market-data-publisher.h"
#include "common/perf-utils.h"

namespace Exchange { 
    MarketDataPublisher::MarketDataPublisher(MEMarketUpdateLFQueue *market_updates, 
        const std::string &iface,
        const std::string &snapshot_ip, int snapshot_port):
        outgoing_md_updates_(market_updates),
        run_(false),
            logger_("exchange_market_data_publisher.log"),
            incremental_socket_(logger_) {
        // ASSERT(incremental_socket_.init(incremental_ip, iface,
        // incremental_port, /*is_listening*/ false) >= 0,
        //     "Unable to create incremental mcast socket. error:" + std::string(std::strerror(errno)));
        incremental_socket_.init();
        snapshot_synthesizer_ = new SnapshotSynthesizer(&snapshot_md_updates_,
             iface, snapshot_ip, snapshot_port);
    }
    auto MarketDataPublisher::run() noexcept -> void {
        while (run_) {
            const auto first_seq_num = next_inc_seq_num_;
            // ToDo - improvement
            // copy the whole memory instead of 1 by 1.
            for (auto market_update = outgoing_md_updates_->getNextReadTo();
                    market_update; market_update = (outgoing_md_updates_->updateReadIndex() ,outgoing_md_updates_->getNextReadTo())) {
                // Read from the market update queue, the ADD and CANCEL updates a request caused.
                if (market_update->type_ == MarketUpdateType::ADD || market_update->type_ == MarketUpdateType::CANCEL)
                    TRACE(M5, traceKey(*market_update), logger_);
                // One send() per message: a message never spans two datagrams.
                const MDPMarketUpdate update{next_inc_seq_num_, *market_update};
                incremental_socket_.send(&update, sizeof(update));
                *snapshot_md_updates_.getNextWriteTo() = update;
                snapshot_md_updates_.updateWriteIndex();
                ++next_inc_seq_num_;
            }
            incremental_socket_.sendAndRecv();
            // This iteration's datagrams are sent: the M5 lines before this one in the log. Only
            // iterations that published something: idle, the loop spins ~5M times a second,
            // logging every one would be ~65 MB of log a second.
            if (next_inc_seq_num_ != first_seq_num) TRACE(M6, next_inc_seq_num_ - 1, logger_);
        }
    }
}