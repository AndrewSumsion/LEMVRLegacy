#pragma once

#include "TcpSocket.h"

namespace lemvr {
    class TcpServer {
    public:
        TcpServer(int socketHandle, void* address, unsigned int* addrlen);
        ~TcpServer();

        static TcpServer* createServer(int port);
        static TcpServer* createServer(int port, const char* ip);

        bool isValid() const { return socketHandle > 0; }

        TcpSocket* accept();
        SocketStatus close();

    private:
        int socketHandle;
        void* address;
        unsigned int* addrlen;
    };
}