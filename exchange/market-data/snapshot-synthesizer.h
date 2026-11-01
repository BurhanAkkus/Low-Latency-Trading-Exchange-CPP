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
  private:
    MDPMarketUpdateLFQueue *snapshot_md_updates_ = nullptr;
    Logger logger_;
    std::atomic<bool> run_ = false;
    std::string time_str_;
    McastSocket snapshot_socket_;
    std::array<std::array<MEMarketUpdate *, ME_MAX_ORDER_IDS>,ME_MAX_TICKERS> ticker_orders_;
    size_t last_inc_seq_num_ = 0;
    Nanos last_snapshot_time_ = 0;
    MemPool<MEMarketUpdate> order_pool_;
  };
}