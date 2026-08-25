#include "../common/time-utils.h"
#include "../common/tcp-server.h"

using namespace Common;

int main(){
    Logger logger_{"../logs/socket-example.log"};
    auto tcpServerRecvCallback = [&](TCPSocket *socket, Nanos rx_time)
    noexcept -> void {
        std::string received_message = std::string(socket->rcv_buffer_,socket->next_rcv_valid_index_);
        logger_.log("TCPServer::recvCallback socket:% msg:% rx:%\n",
                    socket->fd_, 
                    received_message, rx_time);
        const std::string reply = "TCPServer received msg:" +
            std::string(socket->rcv_buffer_, socket-> next_rcv_valid_index_);
        //receive the message from buffer.
        socket->next_rcv_valid_index_ = 0;
        // send reply
        socket->send(reply.data(), reply.length());
    };
    auto tcpServerRecvFinishedCallback = [&]() noexcept -> void {
        logger_.log("TCPServer::defaultRecvFinishedCallback()\n");
    };
    auto tcpClientRecvCallback = [&](TCPSocket* socket, Nanos rx_time)
        noexcept -> void {
            //receive the message from buffer:
            std::string received_message = std::string(socket->rcv_buffer_,socket->next_rcv_valid_index_);
            socket->next_rcv_valid_index_ = 0;
            logger_.log("TCPSocket client recvCallback socket:%, received message:% rx:%\n",socket->fd_,received_message,rx_time);
    };
    // create TCPServer
    std::string iface = "lo";
    const std::string ip = "127.0.0.1";// ip of loopback protocol.
    int port = 12345;
    TCPServer tcp_server{logger_};
    tcp_server.recv_callback_ = tcpServerRecvCallback;
    tcp_server.recv_finished_callback_= tcpServerRecvFinishedCallback;
    tcp_server.listen(iface,port);
    
    // a separate thread for tcp_server.

    // clients.
    std::vector<TCPSocket*> clients(5);
    for(int i = 0; i < clients.size(); i++){
        clients[i] = new TCPSocket{logger_};
        clients[i]->recv_callback_ = tcpClientRecvCallback;
        logger_.log("TCPClient % is connecting to ip:%, iface:%, port:% !\n",i,ip,iface,port);
        clients[i]->connect(ip,iface,port,false);
        tcp_server.poll(); // we process each connection as it comes.
    }
    // a separate thread for clients.

    // simulate messages sent and received.
    using namespace std::literals::chrono_literals;
    // clients send message
    for(int iter = 0; iter < 5; iter++){
        for(int i = 0; i < clients.size();i++){
            std::string  time_str;
            getCurrentTimeStr(&time_str);
            std::string client_message = "Client " + std::to_string(i) + " is sending message " + std::to_string(iter) + " at time: " + time_str;
            clients[i]->send(reinterpret_cast<void*>(client_message.data()),client_message.size());
            clients[i]->sendAndRecv();
            std::this_thread::sleep_for(50ms);
            tcp_server.poll();
            tcp_server.sendAndRecv();
        }
    }
    for(int iter = 0; iter < 5; iter++){
        // clients receive answer.
        for(auto &client:clients){
            client->sendAndRecv();
        }
        tcp_server.poll();
        tcp_server.sendAndRecv();
        std::this_thread::sleep_for(50ms);
    }
    return 0;
}