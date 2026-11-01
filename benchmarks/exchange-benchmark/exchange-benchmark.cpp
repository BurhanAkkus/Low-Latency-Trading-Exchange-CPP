// End-to-end exchange benchmark: clients trade with a running exchange binary the way customers do.
// Every client is a TCP connection to the order server. One thread sends an open-loop request
// stream at a fixed rate: request i is due at start + i / rate, whether or not earlier ones were
// answered. One thread reads every response, one reads the incremental market data on lo.
// Latency starts when a request goes into its TCP connection and ends at its final response, or at
// the market update it caused. A sender that falls behind the schedule is reported as schedule lag.
#include "common/mcast-socket.h"
#include "common/thread-utils.h"
#include "exchange/market-data/market-update.h"
#include "exchange/order-server/client-request.h"
#include "exchange/order-server/client-response.h"
#include <sys/epoll.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <iostream>
#include <random>
#include <unordered_map>

using namespace Common;
using namespace Exchange;

namespace {
  // Where exchange-main listens and publishes.
  constexpr int ORDER_SERVER_PORT = 12345;
  const std::string IFACE = "lo";
  const std::string INCREMENTAL_IP = "233.252.14.3";
  constexpr int INCREMENTAL_PORT = 20001;

  struct Config {
    size_t clients = 255;           // client ids 1..clients, one TCP connection each.
    size_t rate = 100'000;          // requests per second, all clients together.
    size_t seconds = 10;
    // A client cancels each of its orders after this many more of its own requests. An order filled
    // by then gets CANCEL_REJECTED. Resting orders stay below clients * order_lifetime.
    size_t order_lifetime = 50;
    // NEW order prices are uniform in [price_min, price_max], quantities in [1, qty_max].
    Price price_min = 1;
    Price price_max = ME_MAX_PRICE_LEVELS;
    Qty qty_max = 1000;
    unsigned seed = 42;
    int sender_core = 4;
    int receiver_core = 6;
    int md_core = 14;
  };

  auto usage(const char* prog) {
    std::cerr << "usage: " << prog << " [--clients N] [--rate N] [--seconds N] [--order-lifetime N]\n"
              << "       [--price-min N] [--price-max N] [--qty-max N] [--seed N]\n"
              << "       [--sender-core N] [--receiver-core N] [--md-core N]\n"
              << "clients are 1-" << ME_MAX_NUM_CLIENTS - 1 << ", prices in [1, " << ME_MAX_PRICE_LEVELS
              << "], core -1 leaves the thread unpinned.\n";
    std::exit(EXIT_FAILURE);
  }

  auto parseArgs(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
      if (i + 1 >= argc) usage(argv[0]);
      const char* key = argv[i];
      const long value = std::strtol(argv[++i], nullptr, 10);
      if (!std::strcmp(key, "--clients")) cfg.clients = static_cast<size_t>(value);
      else if (!std::strcmp(key, "--rate")) cfg.rate = static_cast<size_t>(value);
      else if (!std::strcmp(key, "--seconds")) cfg.seconds = static_cast<size_t>(value);
      else if (!std::strcmp(key, "--order-lifetime")) cfg.order_lifetime = static_cast<size_t>(value);
      else if (!std::strcmp(key, "--price-min")) cfg.price_min = static_cast<Price>(value);
      else if (!std::strcmp(key, "--price-max")) cfg.price_max = static_cast<Price>(value);
      else if (!std::strcmp(key, "--qty-max")) cfg.qty_max = static_cast<Qty>(value);
      else if (!std::strcmp(key, "--seed")) cfg.seed = static_cast<unsigned>(value);
      else if (!std::strcmp(key, "--sender-core")) cfg.sender_core = static_cast<int>(value);
      else if (!std::strcmp(key, "--receiver-core")) cfg.receiver_core = static_cast<int>(value);
      else if (!std::strcmp(key, "--md-core")) cfg.md_core = static_cast<int>(value);
      else usage(argv[0]);
    }
    if (cfg.clients == 0 || cfg.clients >= ME_MAX_NUM_CLIENTS || cfg.rate == 0 || cfg.seconds == 0) usage(argv[0]);
    if (cfg.order_lifetime == 0 || cfg.qty_max == 0) usage(argv[0]);
    if (cfg.price_min < 1 || cfg.price_min > cfg.price_max || cfg.price_max > ME_MAX_PRICE_LEVELS) usage(argv[0]);
    return cfg;
  }

  // One clock for sends and receives, all in this process.
  auto now() noexcept -> int64_t {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }

  constexpr uint32_t NONE = UINT32_MAX;

  // A NEW order a client will cancel when its request count reaches due.
  struct LiveOrder {
    OrderId order_id;
    TickerId ticker_id;
    Side side;
    Price price;
    size_t due;
  };

  // The request stream, made before the run so sending costs only the send itself.
  struct Requests {
    std::vector<OMClientRequest> stream;
    // Index in stream of each client's NEW and CANCEL, per client order id.
    std::vector<std::vector<uint32_t>> new_index, cancel_index;
  };

  auto makeRequests(const Config& cfg) {
    const size_t n = cfg.rate * cfg.seconds;
    Requests r;
    r.stream.reserve(n);
    r.new_index.resize(cfg.clients + 1);
    r.cancel_index.resize(cfg.clients + 1);
    std::mt19937 rng(cfg.seed);
    std::vector<std::deque<LiveOrder>> live(cfg.clients + 1);
    std::vector<size_t> requests_of(cfg.clients + 1, 0);
    std::array<size_t, ME_MAX_TICKERS> news_of_ticker{};
    for (size_t i = 0; i < n; ++i) {
      const ClientId c = 1 + rng() % cfg.clients;
      auto& orders = live[c];
      MEClientRequest request;
      if (!orders.empty() && orders.front().due <= requests_of[c]) {
        const auto o = orders.front();
        orders.pop_front();
        request = {ClientRequestType::CANCEL, o.order_id, o.ticker_id, c, o.price, 0, o.side};
        r.cancel_index[c][o.order_id] = static_cast<uint32_t>(i);
      } else {
        const OrderId order_id = r.new_index[c].size();
        if (UNLIKELY(order_id >= ME_MAX_ORDER_PER_CLIENT)) FATAL("Out of client order ids, use more --clients or a shorter run.");
        const TickerId ticker_id = rng() % ME_MAX_TICKERS;
        // Market order ids are not reused, the snapshot synthesizer indexes orders by them.
        if (UNLIKELY(++news_of_ticker[ticker_id] > ME_MAX_ORDER_IDS)) FATAL("Out of market order ids, use a shorter run.");
        const Price price = cfg.price_min + rng() % (cfg.price_max - cfg.price_min + 1);
        const Qty qty = 1 + rng() % cfg.qty_max;
        const Side side = (rng() % 2) ? Side::BUY : Side::SELL;
        request = {ClientRequestType::NEW, order_id, ticker_id, c, price, qty, side};
        orders.push_back({order_id, ticker_id, side, price, requests_of[c] + cfg.order_lifetime});
        r.new_index[c].push_back(static_cast<uint32_t>(i));
        r.cancel_index[c].push_back(NONE);
      }
      ++requests_of[c];
      r.stream.push_back({requests_of[c], request});  // seq numbers start at 1.
    }
    return r;
  }

  auto connectClient() -> int {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ORDER_SERVER_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    // The exchange may still be starting up.
    for (int attempt = 0; connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)); ++attempt) {
      if (attempt == 600) FATAL("Can't connect to the order server on port " + std::to_string(ORDER_SERVER_PORT) + ".");
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ASSERT(setNoDelay(fd) && setNonBlocking(fd), "Failed to set up a client socket.");
    return fd;
  }

  // Non-blocking: a full socket buffer means the order server reads slower than we send, keep trying.
  auto sendAll(int fd, const void* data, size_t len) noexcept {
    for (size_t off = 0; off < len;) {
      const auto n = ::send(fd, static_cast<const char*>(data) + off, len - off, MSG_NOSIGNAL);
      if (n > 0) off += n;
      else if (!wouldBlock()) FATAL("send() to the order server failed: " + std::string(strerror(errno)));
    }
  }

  // A final response to a request: NEW gets ACCEPTED or REJECTED, CANCEL gets CANCELED or CANCEL_REJECTED.
  auto isFinal(ClientResponseType type) noexcept {
    return type == ClientResponseType::ACCEPTED || type == ClientResponseType::REJECTED ||
           type == ClientResponseType::CANCELED || type == ClientResponseType::CANCEL_REJECTED;
  }

  struct Response {
    int64_t time;
    uint32_t client_order_id;
    uint32_t market_order_id;
    uint8_t client;
    uint8_t ticker_id;
    ClientResponseType type;
  };

  struct Update {
    int64_t time;
    uint32_t order_id;
    uint8_t ticker_id;
    MarketUpdateType type;
  };

  auto key(uint64_t ticker_id, uint64_t market_order_id) noexcept { return (ticker_id << 32) | market_order_id; }

  // p50 / p90 / p99 / p99.9 / max in microseconds, sorts v.
  auto percentiles(std::vector<int64_t>& v) {
    std::array<double, 5> p{};
    if (v.empty()) return p;
    std::sort(v.begin(), v.end());
    const double at[] = {0.5, 0.9, 0.99, 0.999};
    for (size_t i = 0; i < 4; ++i) p[i] = v[std::min(v.size() - 1, static_cast<size_t>(at[i] * v.size()))] / 1000.0;
    p[4] = v.back() / 1000.0;
    return p;
  }

  auto printPercentiles(const char* name, const std::array<double, 5>& p, size_t samples) {
    printf("%-34s", name);
    for (const auto x : p) printf(" %9.1f", x);
    printf("  (%zu samples)\n", samples);
  }
}

int main(int argc, char** argv) {
  const auto cfg = parseArgs(argc, argv);
  printf("clients:%zu rate:%zu/s seconds:%zu order_lifetime:%zu price:%lu-%lu qty:1-%lu seed:%u cores sender:%d receiver:%d md:%d\n",
         cfg.clients, cfg.rate, cfg.seconds, cfg.order_lifetime, cfg.price_min, cfg.price_max, cfg.qty_max, cfg.seed,
         cfg.sender_core, cfg.receiver_core, cfg.md_core);
  const auto requests = makeRequests(cfg);
  const size_t n = requests.stream.size();

  // Connections, one per client id.
  std::vector<int> fds(cfg.clients + 1, -1);
  for (size_t c = 1; c <= cfg.clients; ++c) fds[c] = connectClient();

  // Not exchange*.log: perf-analysis.py reads those for the engine's measurements.
  Logger logger("benchmark_client.log");
  std::atomic<bool> run{true};

  // Market data: every update's arrival, seq numbers checked for gaps (lost datagrams).
  McastSocket md_socket(logger);
  ASSERT(md_socket.init(INCREMENTAL_IP, IFACE, INCREMENTAL_PORT, true) >= 0 && md_socket.join(INCREMENTAL_IP),
         "Can't join the incremental market data.");
  std::vector<Update> updates;  // ADD and CANCEL only, matched to requests after the run.
  updates.reserve(2 * n);
  size_t md_updates = 0, md_lost = 0, md_next_seq = 0;
  md_socket.recv_callback_ = [&](McastSocket* s) {
    const auto time = now();
    for (size_t off = 0; off + sizeof(MDPMarketUpdate) <= s->next_rcv_valid_index_; off += sizeof(MDPMarketUpdate)) {
      MDPMarketUpdate u;
      memcpy(&u, s->inbound_data_.data() + off, sizeof(u));
      if (md_next_seq && u.seq_num_ > md_next_seq) md_lost += u.seq_num_ - md_next_seq;
      md_next_seq = u.seq_num_ + 1;
      ++md_updates;
      const auto& m = u.me_market_update_;
      if (m.type_ == MarketUpdateType::ADD || m.type_ == MarketUpdateType::CANCEL)
        updates.push_back({time, static_cast<uint32_t>(m.order_id_), static_cast<uint8_t>(m.ticker_id_), m.type_});
    }
    s->next_rcv_valid_index_ = 0;
  };
  auto md_thread = createAndStartThread(cfg.md_core, "Benchmark/MarketData", [&]() { while (run) md_socket.sendAndRecv(); });
  ASSERT(md_thread != nullptr, "Failed to start the market data thread.");

  // Responses: every final response's arrival, per client seq numbers checked for gaps.
  std::vector<Response> responses;
  responses.reserve(n);
  std::atomic<size_t> finals{0};
  size_t fills = 0, tcp_gaps = 0;
  auto receiver_thread = createAndStartThread(cfg.receiver_core, "Benchmark/Receiver", [&]() {
    const int efd = epoll_create1(0);
    for (size_t c = 1; c <= cfg.clients; ++c) {
      epoll_event ev{};
      ev.events = EPOLLIN;
      ev.data.u32 = static_cast<uint32_t>(c);
      ASSERT(epoll_ctl(efd, EPOLL_CTL_ADD, fds[c], &ev) == 0, "epoll_ctl() failed.");
    }
    // Per client: bytes of a response split across two reads.
    struct Buffer { char data[64 * 1024]; size_t size = 0; };
    std::vector<Buffer> buffers(cfg.clients + 1);
    std::vector<size_t> next_seq(cfg.clients + 1, 1);
    epoll_event events[ME_MAX_NUM_CLIENTS];
    while (run) {
      const int ready = epoll_wait(efd, events, ME_MAX_NUM_CLIENTS, 0);
      for (int e = 0; e < ready; ++e) {
        const auto c = events[e].data.u32;
        auto& b = buffers[c];
        const auto got = ::recv(fds[c], b.data + b.size, sizeof(b.data) - b.size, 0);
        if (got <= 0) continue;
        const auto time = now();
        b.size += got;
        size_t off = 0;
        for (; off + sizeof(OMClientResponse) <= b.size; off += sizeof(OMClientResponse)) {
          OMClientResponse r;
          memcpy(&r, b.data + off, sizeof(r));
          tcp_gaps += r.seq_num_ != next_seq[c];
          next_seq[c] = r.seq_num_ + 1;
          const auto& m = r.me_client_response_;
          if (m.type_ == ClientResponseType::FILLED) { ++fills; continue; }
          if (!isFinal(m.type_)) continue;
          responses.push_back({time, static_cast<uint32_t>(m.client_order_id_), static_cast<uint32_t>(m.market_order_id_),
                               static_cast<uint8_t>(c), static_cast<uint8_t>(m.ticker_id_), m.type_});
          finals.fetch_add(1, std::memory_order_relaxed);
        }
        memmove(b.data, b.data + off, b.size - off);
        b.size -= off;
      }
    }
    close(efd);
  });
  ASSERT(receiver_thread != nullptr, "Failed to start the receiver thread.");

  // Sender: request i goes out at start + i / rate.
  std::vector<int64_t> sent_at(n);
  const int64_t start = now() + 100'000'000;  // the other threads are running by then.
  auto due = [&](size_t i) { return start + static_cast<int64_t>(i * 1e9 / cfg.rate); };
  auto sender_thread = createAndStartThread(cfg.sender_core, "Benchmark/Sender", [&]() {
    for (size_t i = 0; i < n; ++i) {
      const auto at = due(i);
      while (now() < at) __builtin_ia32_pause();
      const auto& r = requests.stream[i];
      sent_at[i] = now();  // the request goes into its TCP connection.
      sendAll(fds[r.me_client_request_.client_id_], &r, sizeof(r));
    }
  });
  ASSERT(sender_thread != nullptr, "Failed to start the sender thread.");
  sender_thread->join();

  // Wait for the last answers, then a little longer for the market data.
  const auto sent_end = now();
  while (finals.load(std::memory_order_relaxed) < n && now() - sent_end < 5'000'000'000) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  run = false;
  receiver_thread->join();
  md_thread->join();

  // Responses to requests. The first 10% of requests are warm-up, left out of the latency, as in
  // the matching engine benchmark.
  const size_t warmup = n / 10;
  std::vector<int64_t> response_latency, md_latency, lag;
  response_latency.reserve(n);
  std::unordered_map<uint64_t, int64_t> first_update[2];  // [0] ADD, [1] CANCEL: arrival per (ticker, market order id)
  for (const auto& u : updates) first_update[u.type == MarketUpdateType::CANCEL].emplace(key(u.ticker_id, u.order_id), u.time);
  int64_t last_final = 0;
  for (const auto& r : responses) {
    const bool for_new = r.type == ClientResponseType::ACCEPTED || r.type == ClientResponseType::REJECTED;
    const auto i = (for_new ? requests.new_index : requests.cancel_index)[r.client][r.client_order_id];
    last_final = std::max(last_final, r.time);
    if (i < warmup) continue;
    response_latency.push_back(r.time - sent_at[i]);
    // The update the request caused: ADD when a NEW rests, CANCEL when a CANCEL removes the order.
    if (r.type == ClientResponseType::ACCEPTED || r.type == ClientResponseType::CANCELED) {
      const auto& first = first_update[r.type == ClientResponseType::CANCELED];
      if (const auto it = first.find(key(r.ticker_id, r.market_order_id)); it != first.end()) md_latency.push_back(it->second - sent_at[i]);
    }
  }
  for (size_t i = warmup; i < n; ++i) lag.push_back(sent_at[i] - due(i));

  const double send_secs = (sent_at[n - 1] - sent_at[0]) / 1e9, run_secs = (last_final - sent_at[0]) / 1e9;
  printf("sent %zu requests in %.2f s (%.0f/s)\n", n, send_secs, n / send_secs);
  printf("answered %zu of %zu, %zu fills, tcp seq gaps %zu; market data: %zu updates, %zu lost\n",
         responses.size(), n, fills, tcp_gaps, md_updates, md_lost);
  printf("exchange throughput: %.0f requests/s, %.0f market updates/s\n", responses.size() / run_secs, md_updates / run_secs);
  const auto response_p = percentiles(response_latency), md_p = percentiles(md_latency), lag_p = percentiles(lag);
  printf("%-34s %9s %9s %9s %9s %9s\n", "latency (us), after warm-up", "p50", "p90", "p99", "p99.9", "max");
  printPercentiles("request -> final response", response_p, response_latency.size());
  printPercentiles("request -> ADD / CANCEL update", md_p, md_latency.size());
  printPercentiles("schedule lag (sender behind)", lag_p, lag.size());
  // For run-exchange-benchmark.sh: rates and counts, then p50 p90 p99 p99.9 max of each latency.
  printf("csv:%.0f,%zu,%zu,%zu,%.0f,%.0f", n / send_secs, responses.size(), tcp_gaps, md_lost,
         responses.size() / run_secs, md_updates / run_secs);
  for (const auto& p : {response_p, md_p, lag_p})
    for (const auto x : p) printf(",%.1f", x);
  printf("\n");
  for (size_t c = 1; c <= cfg.clients; ++c) close(fds[c]);
  return responses.size() == n && !tcp_gaps ? EXIT_SUCCESS : EXIT_FAILURE;
}
