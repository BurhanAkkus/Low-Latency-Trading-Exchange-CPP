// Matching engine latency benchmark.
// Stands in for the order server: pushes a reproducible stream of random NEW/CANCEL requests
// straight into the matching engine's request queue at a fixed rate, and drains the response
// and market update queues. The engine logs RDTSC/TTT measurements to
// exchange_matching_engine.log in the current directory, analyse with scripts/perf-analysis.py.
//
// Measures T3 (engine reads request) -> T4/T4t (engine writes output).
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include "matching-engine/matcher/matching-engine.h"

using namespace Exchange;

namespace {
  struct Config {
    size_t requests = 100'000;
    // Keep this above the engine's per request time, otherwise requests queue up and the
    // logger falls behind (its queue overwrites unread lines). 100us saturates the engine today.
    Nanos interval_ns = 500 * NANOS_TO_MICROS;
    unsigned seed = 42;
    unsigned cancel_pct = 25;
    size_t clients = 4;
    int engine_core = 4;   // P-cores are 0-15, pick different physical cores (0-1, 2-3, ... are siblings).
    int logger_core = 6;
    int driver_core = 8;
  };

  auto usage(const char* prog) {
    std::cerr << "usage: " << prog << " [--requests N] [--interval-us N] [--seed N] [--cancel-pct N]\n"
              << "       [--clients N] [--engine-core N] [--logger-core N] [--driver-core N]\n"
              << "core -1 leaves the thread unpinned.\n";
    std::exit(EXIT_FAILURE);
  }

  auto parseArgs(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
      if (i + 1 >= argc) usage(argv[0]);
      const char* key = argv[i];
      const long value = std::strtol(argv[++i], nullptr, 10);
      if (!std::strcmp(key, "--requests")) cfg.requests = static_cast<size_t>(value);
      else if (!std::strcmp(key, "--interval-us")) cfg.interval_ns = value * NANOS_TO_MICROS;
      else if (!std::strcmp(key, "--seed")) cfg.seed = static_cast<unsigned>(value);
      else if (!std::strcmp(key, "--cancel-pct")) cfg.cancel_pct = static_cast<unsigned>(value);
      else if (!std::strcmp(key, "--clients")) cfg.clients = static_cast<size_t>(value);
      else if (!std::strcmp(key, "--engine-core")) cfg.engine_core = static_cast<int>(value);
      else if (!std::strcmp(key, "--logger-core")) cfg.logger_core = static_cast<int>(value);
      else if (!std::strcmp(key, "--driver-core")) cfg.driver_core = static_cast<int>(value);
      else usage(argv[0]);
    }
    if (cfg.clients == 0 || cfg.clients >= ME_MAX_NUM_CLIENTS || cfg.cancel_pct > 100) usage(argv[0]);
    return cfg;
  }
}

int main(int argc, char** argv) {
  const auto cfg = parseArgs(argc, argv);
  std::cout << "requests:" << cfg.requests << " interval_us:" << cfg.interval_ns / NANOS_TO_MICROS
            << " seed:" << cfg.seed << " cancel_pct:" << cfg.cancel_pct << " clients:" << cfg.clients
            << " engine_core:" << cfg.engine_core << " logger_core:" << cfg.logger_core
            << " driver_core:" << cfg.driver_core << std::endl;

  if (cfg.driver_core >= 0)
    ASSERT(setThreadCore(cfg.driver_core), "Failed to pin driver to core " + std::to_string(cfg.driver_core));

  ClientRequestLFQueue requests(ME_MAX_CLIENT_UPDATES);
  ClientResponseLFQueue responses(ME_MAX_CLIENT_UPDATES);
  MEMarketUpdateLFQueue updates(ME_MAX_MARKET_UPDATES);
  auto matching_engine = new MatchingEngine(&requests, &responses, &updates, cfg.logger_core);
  matching_engine->start(cfg.engine_core);

  // random trading client.
  std::mt19937 rng(cfg.seed);
  std::array<OrderId, ME_MAX_NUM_CLIENTS> next_order_id{};
  std::vector<MEClientRequest> new_orders;
  new_orders.reserve(cfg.requests);

  auto next_send = getCurrentNanos();
  for (size_t i = 0; i < cfg.requests; ++i) {
    MEClientRequest request;
    if (!new_orders.empty() && rng() % 100 < cfg.cancel_pct) {
      // Cancel a random earlier order, it may already be filled -> CANCEL_REJECTED.
      request = new_orders[rng() % new_orders.size()];
      request.type_ = ClientRequestType::CANCEL;
    } else {
      const ClientId client_id = 1 + rng() % cfg.clients;
      const OrderId order_id = next_order_id[client_id]++;
      if (UNLIKELY(order_id >= ME_MAX_ORDER_PER_CLIENT)) FATAL("Out of client order ids, use more --clients.");
      const TickerId ticker_id = rng() % ME_MAX_TICKERS;
      const Price price = 100 + rng() % 10 + 1;
      const Qty qty = 1 + rng() % 100;
      const Side side = (rng() % 2) ? Side::BUY : Side::SELL;
      request = {ClientRequestType::NEW, order_id, ticker_id, client_id, price, qty, side};
      new_orders.push_back(request);
    }

    // Spin until the send time, sleep_for overshoots by tens of microseconds.
    next_send += cfg.interval_ns;
    while (getCurrentNanos() < next_send) {
      // Nobody consumes engine output yet, drain it so the queues don't wrap around.
      while (responses.getNextReadTo()) responses.updateReadIndex();
      while (updates.getNextReadTo()) updates.updateReadIndex();
    }

    *requests.getNextWriteTo() = request;
    requests.updateWriteIndex();
  }

  // Let the engine finish the last requests, then shut down (joins the engine thread).
  while (requests.size()) {}
  delete matching_engine;
  std::cout << "done, analyse with: python3 scripts/perf-analysis.py exchange_matching_engine.log --skip-first N"
            << std::endl;
  return EXIT_SUCCESS;
}
