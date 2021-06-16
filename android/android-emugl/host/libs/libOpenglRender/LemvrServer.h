#pragma once

#include "TcpServer.h"
#include "TcpSocket.h"

#include <cstdint>
#include <string>

using std::uint8_t;
using std::uint16_t;

namespace lemvr {
    class LemvrServer {
    public:
        LemvrServer();
        ~LemvrServer();

        int startServer(int port);
        int waitForClient();
        bool hasClientConnected() const { return client != nullptr; }
        int mainLoop();

        int stopServer();
    
    private:
        TcpServer* server;
        TcpSocket* client;
        bool isLittleEndian;

        uint8_t* readPacket(uint8_t& id, SocketStatus& status);
        int handlePacket(uint8_t id, uint8_t* data);
        uint16_t getuint16(uint8_t part1, uint8_t part2);
        
        void packetGetMetadata(uint8_t* data, int& err);
    };
}