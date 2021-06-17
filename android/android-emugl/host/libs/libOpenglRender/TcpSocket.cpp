#include "TcpSocket.h"

#include <errno.h>

#include <iostream> // TODO: Remove this

namespace lemvr {

int socketInit() {
    #ifdef _WIN32
        WSADATA wsaData;
        return WSAStartup(MAKEWORD(2,2), &wsaData);
    #else
        return 0;
    #endif
}

    int socketQuit() {
    #ifdef _WIN32
        return WSACleanup();
    #else
        return 0;
    #endif
}

TcpSocket::TcpSocket(SOCKET socketHandle)
    : socketHandle(socketHandle),
      blocking(true) {}

TcpSocket::~TcpSocket() {}

SocketStatus TcpSocket::write(const std::uint8_t* buffer, int length, int &bytesWritten) {
    ssize_t sent = send(socketHandle, (char*)buffer, (size_t)length, 0);
    if(sent < 0) {
        return errnoToSocketStatus();
    }
    bytesWritten = sent;
    return SocketStatus::OK;
}

SocketStatus TcpSocket::read(const std::uint8_t* buffer, int length, int &bytesRead) {
    ssize_t received = recv(socketHandle, (char*)buffer, length, 0);
    if(received < 0) {
        return errnoToSocketStatus();
    }
    if(received == 0 && length != 0 && !blocking) { // TODO: Not sure why this is necessary. Fix this.
        return SocketStatus::WOULDBLOCK;
    }
    bytesRead = received;
    return SocketStatus::OK;
}

SocketStatus TcpSocket::readAll(const std::uint8_t* buffer, int length) {
    SocketStatus status = SocketStatus::UNKNOWN;
    int totalBytesRead = 0;
    while(totalBytesRead < length) {
        int bytesRead = 0;
        status = read(buffer + totalBytesRead, length - totalBytesRead, bytesRead);
        totalBytesRead += bytesRead;
        if(status != SocketStatus::OK && status != SocketStatus::WOULDBLOCK) {
            return SocketStatus::IOERROR;
        }
    }
    return SocketStatus::OK;
}

SocketStatus setBlockingImpl(bool shouldBlock, SOCKET socketHandle) {
#ifdef _WIN32
   unsigned long mode = shouldBlock ? 0 : 1;
   return (ioctlsocket(socketHandle, FIONBIO, &mode) == 0) ? SocketStatus::OK : SocketStatus::IOERROR;
#else
   int flags = fcntl(socketHandle, F_GETFL, 0);
   if (flags == -1) return SocketStatus::IOERROR;
   flags = shouldBlock ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
   return (fcntl(socketHandle, F_SETFL, flags) == 0) ? SocketStatus::OK : SocketStatus::IOERROR;
#endif
}

SocketStatus TcpSocket::setBlocking(bool shouldBlock) {
    SocketStatus status = setBlockingImpl(shouldBlock, socketHandle);
    if(status == SocketStatus::OK) {
        blocking = shouldBlock;
    }
    return status;
}

namespace {
    int closeImpl(SOCKET handle) {
        int status = 0;

        #ifdef _WIN32
            status = shutdown(handle, SD_BOTH);
            if (status == 0) { status = closesocket(handle); }
        #else
            status = shutdown(handle, SHUT_RDWR);
            if (status == 0) { status = close(handle); }
        #endif

        return status;
    }
}

SocketStatus TcpSocket::close() {
    if(closeImpl(socketHandle) < 0) {
        return errnoToSocketStatus();
    }
    
    return SocketStatus::OK;
}

//static
SocketStatus TcpSocket::errnoToSocketStatus() {
    if(errno > 0) {
        if(errno == EWOULDBLOCK) {
            return SocketStatus::WOULDBLOCK;
        } else {
            return SocketStatus::IOERROR;
        }
    } else {
        return SocketStatus::OK;
    }
}

}