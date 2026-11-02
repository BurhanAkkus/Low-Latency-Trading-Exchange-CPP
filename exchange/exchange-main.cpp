#include <csignal>
#include <cstring>
#include <iostream>
#include "exchange/matching-engine/matcher/matching-engine.h"
#include "exchange/market-data/market-data-publisher.h"
#include "exchange/order-server/order-server.h"
Common::Logger *logger = nullptr;
Exchange::MatchingEngine *matching_engine = nullptr;
Exchange::MarketDataPublisher *market_data_publisher = nullptr;
Exchange::OrderServer *order_server = nullptr;
// Only async-signal-safe work is allowed in a signal handler,
// so it just raises a flag and main() does the shutdown.
volatile std::sig_atomic_t shutdown_requested = 0;
void signal_handler(int) {
  shutdown_requested = 1;
}

namespace {
  // Cores for the busy threads, -1 leaves a thread unpinned.
  struct Cores {
    int engine = -1;
    int engine_logger = -1;  // the engine logs every request (ME_req).
    int order_server = -1;
    int mdp = -1;
    int snapshot = -1;
  };

  auto usage(const char* prog) {
    std::cerr << "usage: " << prog << " [--engine-core N] [--engine-logger-core N] [--order-server-core N]\n"
              << "       [--mdp-core N] [--snapshot-core N]\n"
              << "core -1 (the default) leaves the thread unpinned.\n";
    std::exit(EXIT_FAILURE);
  }

  auto parseArgs(int argc, char** argv) {
    Cores cores;
    for (int i = 1; i < argc; ++i) {
      if (i + 1 >= argc) usage(argv[0]);
      const char* key = argv[i];
      const int value = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
      if (!std::strcmp(key, "--engine-core")) cores.engine = value;
      else if (!std::strcmp(key, "--engine-logger-core")) cores.engine_logger = value;
      else if (!std::strcmp(key, "--order-server-core")) cores.order_server = value;
      else if (!std::strcmp(key, "--mdp-core")) cores.mdp = value;
      else if (!std::strcmp(key, "--snapshot-core")) cores.snapshot = value;
      else usage(argv[0]);
    }
    return cores;
  }
}

int main(int argc, char **argv) {
    // First, a signal before this stops the process with nothing to clean up yet. SIGTERM too: a
    // script's background commands ignore SIGINT until a handler is set, run-exchange-benchmark.sh
    // stops the exchange with SIGTERM.
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    const auto cores = parseArgs(argc, argv);
    logger = new Common::Logger("exchange_main.log");
    const int sleep_time = 100 * 1000;
    // Communication queues.
    Exchange::ClientRequestLFQueue client_requests;
    Exchange::ClientResponseLFQueue client_responses;
    Exchange::MEMarketUpdateLFQueue market_updates;
    std::string time_str;
    // Start matching engine.
    logger->log("%:% %() % Starting Matching Engine...\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
    matching_engine = new Exchange::MatchingEngine(&client_requests, &client_responses, &market_updates, cores.engine_logger);
    matching_engine->start(cores.engine);
    // start market data publisher
    const std::string mkt_pub_iface = "lo";
    // The incremental stream is in shared memory (market-data-channel.h), not on 233.252.14.3:20001.
    // const std::string snap_pub_ip = "233.252.14.1", inc_pub_ip ="233.252.14.3";
    // const int snap_pub_port = 20000, inc_pub_port = 20001;
    const std::string snap_pub_ip = "233.252.14.1";
    const int snap_pub_port = 20000;
    logger->log("%:% %() % Starting Market Data Publisher...\n", __FILE__, __LINE__, __FUNCTION__,
         Common::getCurrentTimeStr(&time_str));
    // market_data_publisher = new Exchange::MarketDataPublisher(&market_updates, mkt_pub_iface,
    //     snap_pub_ip, snap_pub_port, inc_pub_ip, inc_pub_port);
    market_data_publisher = new Exchange::MarketDataPublisher(&market_updates, mkt_pub_iface,
        snap_pub_ip, snap_pub_port);
    market_data_publisher->start(cores.mdp, cores.snapshot);
    // order gateway.
    const std::string order_gw_iface = "lo";
    const int order_gw_port = 12345;
    logger->log("%:% %() % Starting Order Server...\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
    order_server = new Exchange::OrderServer(&client_requests, &client_responses, order_gw_iface, order_gw_port);
    order_server->start(cores.order_server);
    // Heartbeat
    // usleep returns early when SIGINT or SIGTERM arrives, so shutdown is not delayed by the sleep.
    while (!shutdown_requested) {
        logger->log("%:% %() % Sleeping for a few milliseconds..\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
        usleep(sleep_time * 1000);
    }
    // Stop taking requests first: the order server is not left waiting for space in the request
    // queue of a stopped engine. The engine then finishes the requests already queued, and the
    // publisher goes last, after the engine's last market updates.
    logger->log("%:% %() % Stopping...\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
    delete order_server; order_server = nullptr;
    delete matching_engine; matching_engine = nullptr;
    delete market_data_publisher; market_data_publisher = nullptr;
    delete logger; logger = nullptr;
    // The queues are destroyed after this, once nothing uses them.
    return EXIT_SUCCESS;
}