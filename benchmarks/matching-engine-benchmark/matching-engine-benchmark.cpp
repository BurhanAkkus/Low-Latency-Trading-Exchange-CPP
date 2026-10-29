// Matching engine latency benchmark.
// Stands in for the order server: pushes a reproducible stream of random NEW/CANCEL requests
// straight into the matching engine's request queue, back to back by default, and reads the
// client responses in the same thread. A thread on its own core stands in for the market data
// publisher and reads the market updates. Both only drain their queue, they don't publish. The
// engine logs RDTSC/TTT measurements to exchange_matching_engine.log in the current directory,
// analyse with scripts/perf-analysis.py.
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
    // 0 sends back to back: the driver is ~3x faster than the engine, so the request queue fills
    // up, the driver waits for space and the engine runs with warm caches, never waiting for a
    // request. The engine keeps up from 1us.
    Nanos interval_ns = 0;
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
    int mdp_core = 10;     // market data publisher stand-in.
  };

  // Copies the message out like a real consumer does, which pulls its cache line from the
  // engine's core. The empty asm reads the copy, so the compiler can't drop the loads.
  template<typename T>
  auto consume(const T* message) noexcept {
    const T copy = *message;
    asm volatile("" : : "m"(copy));
  }

  auto usage(const char* prog) {
    std::cerr << "usage: " << prog << " [--requests N] [--interval-us N] [--seed N] [--cancel-pct N]\n"
              << "       [--clients N] [--price-min N] [--price-max N]\n"
              << "       [--engine-core N] [--logger-core N] [--driver-core N] [--mdp-core N]\n"
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
      else if (!std::strcmp(key, "--mdp-core")) cfg.mdp_core = static_cast<int>(value);
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
            << " driver_core:" << cfg.driver_core << " mdp_core:" << cfg.mdp_core << std::endl;

  if (cfg.driver_core >= 0)
    ASSERT(setThreadCore(cfg.driver_core), "Failed to pin driver to core " + std::to_string(cfg.driver_core));

  ClientRequestLFQueue requests;
  ClientResponseLFQueue responses;
  MEMarketUpdateLFQueue updates;

  // Market data publisher: polls the market updates on its own core until stopped.
  std::atomic<bool> mdp_run{true};
  size_t updates_read = 0;
  auto mdp_thread = createAndStartThread(cfg.mdp_core, "Benchmark/MarketDataPublisher", [&]() {
    while (true) {
      // Read mdp_run before the queue: once it reads false, this drain sees every update.
      const bool stopping = !mdp_run;
      for (auto update = updates.getNextReadTo(); update; update = updates.getNextReadTo()) {
        consume(update);
        updates.updateReadIndex();
        updates_read++;
      }
      if (stopping) return;
    }
  });
  ASSERT(mdp_thread != nullptr, "Failed to start the market data publisher thread.");

  // Order server: reads the client responses in the driver's thread, between sends.
  size_t responses_read = 0;
  auto drain_responses = [&]() {
    for (auto response = responses.getNextReadTo(); response; response = responses.getNextReadTo()) {
      consume(response);
      responses.updateReadIndex();
      responses_read++;
    }
  };

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

    // Spin until the send time, sleep_for overshoots by tens of microseconds. Like the order
    // server, keep reading responses while waiting.
    next_send += cfg.interval_ns;
    while (getCurrentNanos() < next_send) drain_responses();
    // Not getNextWriteTo()'s wait: the engine may be waiting for space in the response queue.
    while (UNLIKELY(!requests.hasSpace())) drain_responses();

    *requests.getNextWriteTo() = request;
    requests.updateWriteIndex();
    drain_responses();
  }

  // The engine processes the requests still queued, then its thread is joined. Deleted from
  // another thread so this one keeps reading responses, the engine may need the space.
  std::atomic<bool> engine_stopped{false};
  std::thread stopper([&]() { delete matching_engine; engine_stopped = true; });
  while (!engine_stopped) drain_responses();
  stopper.join();
  drain_responses();
  mdp_run = false;
  mdp_thread->join();
  delete mdp_thread;
  std::cout << "order server read " << responses_read << " client responses, market data publisher read "
            << updates_read << " market updates" << std::endl;
  std::cout << "done, analyse with: python3 scripts/perf-analysis.py exchange_matching_engine.log --skip-first N"
            << std::endl;
  return EXIT_SUCCESS;
}
