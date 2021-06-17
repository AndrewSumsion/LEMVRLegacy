#include "LemvrServer.h"

#include <iostream>

using std::uint8_t;
using std::uint16_t;

namespace lemvr {

LemvrServer::LemvrServer()
    : server(nullptr),
      client(nullptr),
      isLittleEndian(false) {
    uint16_t num = 1;
    isLittleEndian = *(uint8_t*)&num == 1;
}

LemvrServer::~LemvrServer() {
}

int LemvrServer::startServer(int port) {
    server = TcpServer::createServer(5892);

    if(!server->isValid()) {
        return 1;
    }
    return 0;
}

int LemvrServer::stopServer() {
    client->close();
    server->close();
    return 0;
}

int LemvrServer::waitForClient() {
    if(client) {
        client->close();
        delete client;
        client = nullptr;
    }

    TcpSocket* acceptedClient = server->accept();
    if(!acceptedClient->isValid()) {
        return 1;
    }

    uint8_t recvBuffer[4] = {0};
    int bytes = 0;

    if(acceptedClient->readAll(recvBuffer, 4) != SocketStatus::OK) {
        return 1;
    }

    if(!(recvBuffer[0] == 2 && recvBuffer[1] == 1 && recvBuffer[2] == 8 && recvBuffer[3] == 7)) {
        return 2;
    }

    uint8_t sendBuffer[] = {1, 1, 3, 8};

    if(acceptedClient->write(sendBuffer, 4, bytes) != SocketStatus::OK) {
        return 1;
    }

    if(acceptedClient->setBlocking(false) != SocketStatus::OK) {
        return 1;
    }

    client = acceptedClient;
    return 0;
}

int LemvrServer::mainLoop() {
    SocketStatus status = SocketStatus::OK;
    while(true) {
        uint8_t id = 0;
        uint8_t* buffer = readPacket(id, status);
        if(status != SocketStatus::OK) {
            break;
        }
            
        int packetErr = handlePacket(id, buffer);
        if(packetErr != 0) {
            std::cerr << "Error occured handling packet. id=" << id << std::endl;
            return packetErr;
        }
    }

    if(status != SocketStatus::WOULDBLOCK) {
        std::cerr << "Error occured reading packet. status=" << (int)status << std::endl;
        return (int)status;
    }

    return 0;
}

uint8_t* LemvrServer::readPacket(uint8_t& id, SocketStatus& result) {
    uint8_t recvBuffer[3] = {};
    int bytes = 0;

    SocketStatus status = SocketStatus::UNKNOWN;

    status = client->read(recvBuffer, 3, bytes);
    if(status != SocketStatus::OK) {
        result = status;
        return nullptr;
    }
    if(bytes == 0) {
        result = SocketStatus::WOULDBLOCK;
        return nullptr;
    }

    uint8_t packetId = recvBuffer[0];
    uint16_t size = getuint16(recvBuffer[1], recvBuffer[2]);
    uint8_t* buffer = new uint8_t[size];

    status = client->readAll(buffer, size);
    if(status != SocketStatus::OK) {
        result = status;
        return nullptr;
    }
        
    id = packetId;
    result = SocketStatus::OK;
    return buffer;
}

int LemvrServer::handlePacket(uint8_t id, uint8_t* buffer) {
    int err = 0;

    if(id == 3) {
        packetGetMetadata(buffer, err);
    }

    delete[] buffer;
    return err;
}

uint16_t LemvrServer::getuint16(uint8_t part1, uint8_t part2) {
    if(isLittleEndian) {
        return (uint16_t)part1 << 8 | (uint16_t)part2;
    } else {
        return (uint16_t)part2 << 8 | (uint16_t)part1;
    }
}

void LemvrServer::packetGetMetadata(uint8_t* data, int& err) {
    std::cout << "packetGetMetadata received: " << (int)data[0] << std::endl;
}

} // namespace lemvr
