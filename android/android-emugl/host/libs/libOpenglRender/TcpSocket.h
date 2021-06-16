#pragma once

#include <cstdint>

namespace lemvr {
    enum class SocketStatus {
        UNKNOWN = 0,
        OK = 1,
        WOULDBLOCK = 2,
        ERROR = 3
    };

    class TcpSocket {
    public:
        TcpSocket(int socketHandle);
        ~TcpSocket();

    public:
        SocketStatus write(std::uint8_t* buffer, int length, int &bytesWritten);
        SocketStatus read(std::uint8_t* buffer, int length, int &bytesRead);
        SocketStatus readAll(std::uint8_t* buffer, int length);
        SocketStatus setBlocking(bool shouldBlock);
        bool isBlocking() const { return blocking; }
        SocketStatus close();

        static SocketStatus errnoToSocketStatus();

        bool isValid() const { return socketHandle > 0; }

    private:
        int socketHandle;
        bool blocking;
    };
}