#include "exchange/order-server/order-server.h"

using namespace Common;

namespace Exchange{
    OrderServer::OrderServer(ClientRequestLFQueue* client_requests,
            ClientResponseLFQueue* client_responses, const std::string &iface,
            int port):iface_{iface},port_{port},
                client_requests_{client_requests}, client_responses_{client_responses},
                logger_("exchange_order_server.log"),tcp_server_{logger_},
                fifo_sequencer_(client_requests){
                    cid_next_outgoing_seq_num_.fill(1);
                    cid_next_expected_seq_num_.fill(1);
                    client_to_port_.fill(nullptr);
                    tcp_server_.recv_callback_ = [this](auto socket, auto rx_time) { recvCallback(socket, rx_time); };
                    tcp_server_.recv_finished_callback_ = [this]() { recvFinishedCallback(); };
            };
    auto OrderServer::start() noexcept -> void {
        run_ = true;
        tcp_server_.listen(iface_, port_);
        ASSERT(Common::createAndStartThread(-1, "Exchange/OrderServer",
        [this]() { run(); }) != nullptr, "Failed to start OrderServer thread.");
    }

    auto OrderServer::stop() noexcept -> void {
        run_ = false;
    }
    OrderServer::~OrderServer() {
        stop();
        using namespace std::literals::chrono_literals;
        std::this_thread::sleep_for(1s);
    }
}