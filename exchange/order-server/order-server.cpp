#include "exchange/order-server/order-server.h"

using namespace Common;

namespace Exchange{
    OrderServer::OrderServer(ClientRequestLFQueue* client_requests,
            ClientResponseLFQueue* client_responses, const std::string &iface,
            int port):iface_{iface},port_{port},
                client_requests_{client_requests}, client_responses_{client_responses},
                logger_("exchange_order_server.log"),
                tcp_server_{logger_, [this](auto socket, auto rx_time) { recvCallback(socket, rx_time); },
                    [this]() { recvFinishedCallback(); }},
                fifo_sequencer_(client_requests, [this]() { sendClientResponses(); }){
                    cid_next_outgoing_seq_num_.fill(1);
                    cid_next_expected_seq_num_.fill(1);
                    client_to_port_.fill(nullptr);
                    tcp_server_.disconnect_callback_ = [this](auto socket) {
                        for (auto &client_socket : client_to_port_)
                            if (client_socket == socket) client_socket = nullptr;
                    };

            };

    auto OrderServer::run() noexcept -> void {
      while (run_) {
        tcp_server_.poll();
        tcp_server_.sendAndRecv();
        sendClientResponses();
        }
    }
    // Writes the responses to the clients' send buffers, the next sendAndRecv() sends them.
    auto OrderServer::sendClientResponses() noexcept -> void {
        // ToDo - improvement
        // send all of them together as a memcpy operation.
        for (auto client_response = client_responses_->getNextReadTo();
                client_response; client_response = (client_responses_->updateReadIndex(),client_responses_->getNextReadTo())) {
            if(UNLIKELY(client_to_port_[client_response->client_id_] == nullptr)){continue;}
            auto &next_outgoing_seq_num = cid_next_outgoing_seq_num_[client_response->client_id_];
            client_to_port_[client_response->client_id_]->send(&next_outgoing_seq_num, sizeof(next_outgoing_seq_num));
            client_to_port_[client_response->client_id_]->send(client_response, sizeof(MEClientResponse));
            ++next_outgoing_seq_num;
            }
    }
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