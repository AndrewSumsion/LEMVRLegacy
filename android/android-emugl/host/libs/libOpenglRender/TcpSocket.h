#pragma once

#ifdef _WIN32
  #include <winsock2.h>
  #include <Ws2tcpip.h>
  #include <BaseTsd.h>
  typedef SSIZE_T ssize_t;
#else
  typedef int SOCKET;
  #include <sys/socket.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
#endif

#include <cstdint>

namespace lemvr {
    enum class SocketStatus {
        UNKNOWN = 0,
        OK = 1,
        WOULDBLOCK = 2,
        IOERROR = 3
    };

    int socketInit();
    int socketQuit();

    class TcpSocket {
    public:
        TcpSocket(SOCKET socketHandle);
        ~TcpSocket();

    public:
        SocketStatus write(const std::uint8_t* buffer, int length, int &bytesWritten);
        SocketStatus read(const std::uint8_t* buffer, int length, int &bytesRead);
        SocketStatus readAll(const std::uint8_t* buffer, int length);
        SocketStatus setBlocking(bool shouldBlock);
        bool isBlocking() const { return blocking; }
        SocketStatus close();

        static SocketStatus errnoToSocketStatus();

#ifdef _WIN32
        bool isValid() const { return socketHandle >= 0; }
#else
        bool isValid() const { return socketHandle != INVALID_SOCKET; }
#endif

    private:
        SOCKET socketHandle;
        bool blocking;
    };
}