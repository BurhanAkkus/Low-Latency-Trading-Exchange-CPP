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
                incremental_socket_.send(&next_inc_seq_num_, sizeof(next_inc_seq_num_));
                incremental_socket_.send(market_update,sizeof(MEMarketUpdate));
                auto next_write = snapshot_md_updates_.getNextWriteTo();
                next_write->seq_num_ = next_inc_seq_num_;
                next_write->me_market_update_ = *market_update;
                snapshot_md_updates_.updateWriteIndex();
                ++next_inc_seq_num_;
            }
            incremental_socket_.sendAndRecv();
        }
    }
}