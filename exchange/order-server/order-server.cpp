#include "exchange/order-server/order-server.h"
#include "common/perf-utils.h"

using namespace Common;

namespace Exchange{
    OrderServer::OrderServer(ClientRequestLFQueue* client_requests,
            ClientResponseLFQueue* client_responses, const std::string &iface,
            int port):iface_{iface},port_{port},
                client_requests_{client_requests}, client_responses_{client_responses},
                logger_("exchange_order_server.log"),
                tcp_server_{logger_, [this](auto socket, auto rx_time) { recvCallback(socket, rx_time); },
                    [this]() { recvFinishedCallback(); }},
                fifo_sequencer_(client_requests, &logger_, [this]() { sendClientResponses(); }){
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
    // Writes the responses to the clients' send buffers and sends them.
    auto OrderServer::sendClientResponses() noexcept -> void {
        // ToDo - improvement
        // send all of them together as a memcpy operation.
        for (auto client_response = client_responses_->getNextReadTo();
                client_response; client_response = (client_responses_->updateReadIndex(),client_responses_->getNextReadTo())) {
            auto socket = client_to_port_[client_response->client_id_];
            if(UNLIKELY(socket == nullptr)){continue;}
            // Final responses only: a fill isn't the end of a request.
            if (client_response->type_ != ClientResponseType::FILLED)
                TRACE(T5, traceKey(*client_response), logger_);
            auto &next_outgoing_seq_num = cid_next_outgoing_seq_num_[client_response->client_id_];
            tcp_server_.send(socket, &next_outgoing_seq_num, sizeof(next_outgoing_seq_num));
            tcp_server_.send(socket, client_response, sizeof(MEClientResponse));
            ++next_outgoing_seq_num;
            }
        // One send per socket, with all of its responses.
        tcp_server_.flush();
    }
    auto OrderServer::start(int core_id) noexcept -> void {
        run_ = true;
        tcp_server_.listen(iface_, port_);
        thread_ = Common::createAndStartThread(core_id, "Exchange/OrderServer", [this]() { run(); });
        ASSERT(thread_ != nullptr, "Failed to start OrderServer thread.");
    }

    auto OrderServer::stop() noexcept -> void {
        run_ = false;
    }
    OrderServer::~OrderServer() {
        stop();
        if (thread_) { thread_->join(); delete thread_; thread_ = nullptr; }
    }
}