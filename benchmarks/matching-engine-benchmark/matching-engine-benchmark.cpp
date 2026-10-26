// Matching engine latency benchmark.
// Stands in for the order server: pushes a reproducible stream of random NEW/CANCEL requests
// straight into the matching engine's request queue, back to back by default. Nothing reads the
// response and market update queues, the engine overwrites them. The engine logs RDTSC/TTT
// measurements to exchange_matching_engine.log in the current directory, analyse with
// scripts/perf-analysis.py.
//
// Measures T3 (engine reads request) -> T4/T4t (engine writes output).
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include "exchange/matching-engine/matcher/matching-engine.h"

using namespace Exchange;

namespace {
  struct Config {
    size_t requests = 200'000;
    // 0 sends back to back: the driver is ~3x faster than the engine, so requests queue up and
    // the engine runs with warm caches. The engine keeps up from 1us.
    Nanos interval_ns = 0;
    // With no gap, after the first burst_requests send one request every paced_ns, about the
    // engine's per-request time (~110ns). Back to back for the whole run the driver gets more than
    // the request queue's 262144 ahead of the engine and overwrites requests it hasn't read.
    size_t burst_requests = 200'000;
    Nanos paced_ns = 100;
    unsigned seed = 42;
    unsigned cancel_pct = 25;
    // Each client has ME_MAX_ORDER_PER_CLIENT order ids, 16 covers the default requests.
    size_t clients = 16;
    // NEW order prices are uniform in [price_min, price_max]. The whole book by default: a sparse
    // book with far apart levels. A narrow range (e.g. 101-110) is a dense book.
    Price price_min = 1;
    Price price_max = ME_MAX_PRICE_LEVELS;
    int engine_core = 4;   // P-cores are 0-15, pick different physical cores (0-1, 2-3, ... are siblings).
    int logger_core = 6;
    int driver_core = 8;
  };

  auto usage(const char* prog) {
    std::cerr << "usage: " << prog << " [--requests N] [--interval-us N] [--seed N] [--cancel-pct N]\n"
              << "       [--clients N] [--price-min N] [--price-max N]\n"
              << "       [--engine-core N] [--logger-core N] [--driver-core N]\n"
              << "prices are in [1, " << ME_MAX_PRICE_LEVELS << "], core -1 leaves the thread unpinned.\n";
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
      else if (!std::strcmp(key, "--price-min")) cfg.price_min = static_cast<Price>(value);
      else if (!std::strcmp(key, "--price-max")) cfg.price_max = static_cast<Price>(value);
      else if (!std::strcmp(key, "--engine-core")) cfg.engine_core = static_cast<int>(value);
      else if (!std::strcmp(key, "--logger-core")) cfg.logger_core = static_cast<int>(value);
      else if (!std::strcmp(key, "--driver-core")) cfg.driver_core = static_cast<int>(value);
      else usage(argv[0]);
    }
    if (cfg.clients == 0 || cfg.clients >= ME_MAX_NUM_CLIENTS || cfg.cancel_pct > 100) usage(argv[0]);
    if (cfg.price_min < 1 || cfg.price_min > cfg.price_max || cfg.price_max > ME_MAX_PRICE_LEVELS) usage(argv[0]);
    return cfg;
  }
}

int main(int argc, char** argv) {
  const auto cfg = parseArgs(argc, argv);
  std::cout << "requests:" << cfg.requests << " interval_us:" << cfg.interval_ns / NANOS_TO_MICROS
            << " seed:" << cfg.seed << " cancel_pct:" << cfg.cancel_pct << " clients:" << cfg.clients
            << " price:" << cfg.price_min << "-" << cfg.price_max
            << " engine_core:" << cfg.engine_core << " logger_core:" << cfg.logger_core
            << " driver_core:" << cfg.driver_core << std::endl;

  if (cfg.driver_core >= 0)
    ASSERT(setThreadCore(cfg.driver_core), "Failed to pin driver to core " + std::to_string(cfg.driver_core));

  ClientRequestLFQueue requests;
  ClientResponseLFQueue responses;
  MEMarketUpdateLFQueue updates;
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
      const Price price = cfg.price_min + rng() % (cfg.price_max - cfg.price_min + 1);
      const Qty qty = 1 + rng() % 100;
      const Side side = (rng() % 2) ? Side::BUY : Side::SELL;
      request = {ClientRequestType::NEW, order_id, ticker_id, client_id, price, qty, side};
      new_orders.push_back(request);
    }

    // Spin until the send time, sleep_for overshoots by tens of microseconds.
    const bool paced = cfg.interval_ns == 0 && i >= cfg.burst_requests;
    // The paced schedule starts now, the burst has left next_send far behind.
    if (paced && i == cfg.burst_requests) next_send = getCurrentNanos();
    next_send += paced ? cfg.paced_ns : cfg.interval_ns;
    while (getCurrentNanos() < next_send) {}

    *requests.getNextWriteTo() = request;
    requests.updateWriteIndex();
  }

  // The engine processes the requests still queued, then its thread is joined.
  delete matching_engine;
  std::cout << "done, analyse with: python3 scripts/perf-analysis.py exchange_matching_engine.log --skip-first N"
            << std::endl;
  return EXIT_SUCCESS;
}
