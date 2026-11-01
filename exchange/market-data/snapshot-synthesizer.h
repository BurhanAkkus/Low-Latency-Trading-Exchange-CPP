#pragma once
#include "common/types.h"
#include "common/thread-utils.h"
#include "common/lf-queue.h"
#include "common/macros.h"
#include "common/mcast-socket.h"
#include "common/memory-pool.h"
#include "common/logging.h"
#include "exchange/market-data/market-update.h"
#include "exchange/matching-engine/matcher/me-order.h"
using namespace Common;
namespace Exchange {
  class SnapshotSynthesizer {
  public:
    SnapshotSynthesizer(MDPMarketUpdateLFQueue *market_updates, const std::string &iface,
                        const std::string &snapshot_ip, int snapshot_port);
    ~SnapshotSynthesizer();
    // core_id -1 leaves the thread unpinned.
    auto start(int core_id = -1) -> void;
    auto stop() -> void;
  private:
    std::thread* thread_ = nullptr;
    auto run() -> void;
    auto addToSnapshot(const MDPMarketUpdate *market_update) -> void;
    auto publishSnapshot() -> void;
    MDPMarketUpdateLFQueue *snapshot_md_updates_ = nullptr;
    Logger logger_;
    std::atomic<bool> run_ = false;
    std::string time_str_;
    McastSocket snapshot_socket_;
    // Every slot starts as nullptr: no order with that id yet.
    std::array<std::array<MEMarketUpdate *, ME_MAX_ORDER_IDS>,ME_MAX_TICKERS> ticker_orders_{};
    size_t last_inc_seq_num_ = 0;
    Nanos last_snapshot_time_ = 0;
    // Every order resting in any book: the engine can't rest more than this.
    MemoryPool<MEMarketUpdate, ME_MAX_RESTING_ORDERS * ME_MAX_TICKERS> order_pool_;
  };
}