#pragma once

#include "TcpSocket.h"

namespace lemvr {
    class TcpServer {
    public:
        TcpServer(SOCKET socketHandle, void* address, unsigned int* addrlen);
        ~TcpServer();

        static TcpServer* createServer(int port);
        static TcpServer* createServer(int port, const char* ip);

#ifdef _WIN32
        bool isValid() const { return socketHandle >= 0; }
#else
        bool isValid() const { return socketHandle != INVALID_SOCKET; }
#endif

        TcpSocket* accept();
        SocketStatus close();

    private:
        SOCKET socketHandle;
        void* address;
        unsigned int* addrlen;
    };
}