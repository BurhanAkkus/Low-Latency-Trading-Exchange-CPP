#include "exchange/market-data/market-data-publisher.h"

namespace Exchange { 
    MarketDataPublisher::MarketDataPublisher(MEMarketUpdateLFQueue *market_updates, 
        const std::string &iface,
        const std::string &snapshot_ip, int snapshot_port,
        const std::string&incremental_ip, int incremental_port):
        outgoing_md_updates_(market_updates), 
        snapshot_md_updates_(ME_MAX_MARKET_UPDATES),
        run_(false),
            logger_("exchange_market_data_publisher.log"),
            incremental_socket_(logger_) {
        ASSERT(incremental_socket_.init(incremental_ip, iface,
        incremental_port, /*is_listening*/ false) >= 0,
            "Unable to create incremental mcast socket. error:" + std::string(std::strerror(errno)));
        snapshot_synthesizer_ = new SnapshotSynthesizer(&snapshot_md_updates_,
             iface, snapshot_ip, snapshot_port);
    }
    auto MarketDataPublisher::run() noexcept -> void {
        while (run_) {
            // ToDo - improvement
            // copy the whole memory instead of 1 by 1.
            for (auto market_update = outgoing_md_updates_->getNextReadTo();
                    market_update; market_update = (outgoing_md_updates_->updateReadIndex() ,outgoing_md_updates_->getNextReadTo())) {
                // One send() per message: a message never spans two datagrams.
                const MDPMarketUpdate update{next_inc_seq_num_, *market_update};
                incremental_socket_.send(&update, sizeof(update));
                *snapshot_md_updates_.getNextWriteTo() = update;
                snapshot_md_updates_.updateWriteIndex();
                ++next_inc_seq_num_;
            }
            incremental_socket_.sendAndRecv();
        }
    }
}