#include "TcpSocket.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <iostream> // TODO: Remove this

namespace lemvr {

TcpSocket::TcpSocket(int socketHandle)
    : socketHandle(socketHandle),
      blocking(true) {}

TcpSocket::~TcpSocket() {}

SocketStatus TcpSocket::write(std::uint8_t* buffer, int length, int &bytesWritten) {
    ssize_t sent = send(socketHandle, buffer, (size_t)length, 0);
    if(sent < 0) {
        return errnoToSocketStatus();
    }
    bytesWritten = sent;
    return SocketStatus::OK;
}

SocketStatus TcpSocket::read(std::uint8_t* buffer, int length, int &bytesRead) {
    ssize_t received = recv(socketHandle, buffer, length, 0);
    if(received < 0) {
        return errnoToSocketStatus();
    }
    if(received == 0 && length != 0 && !blocking) { // TODO: Not sure why this is necessary. Fix this.
        return SocketStatus::WOULDBLOCK;
    }
    bytesRead = received;
    return SocketStatus::OK;
}

SocketStatus TcpSocket::readAll(std::uint8_t* buffer, int length) {
    SocketStatus status = SocketStatus::UNKNOWN;
    int totalBytesRead = 0;
    while(totalBytesRead < length) {
        int bytesRead = 0;
        status = read(buffer + totalBytesRead, length - totalBytesRead, bytesRead);
        totalBytesRead += bytesRead;
        if(status != SocketStatus::OK && status != SocketStatus::WOULDBLOCK) {
            return SocketStatus::ERROR;
        }
    }
    return SocketStatus::OK;
}

SocketStatus setBlockingImpl(bool shouldBlock, int socketHandle) {
#ifdef _WIN32
   unsigned long mode = shouldBlock ? 0 : 1;
   return (ioctlsocket(socketHandle, FIONBIO, &mode) == 0) ? SocketStatus::OK : SocketStatus::ERROR;
#else
   int flags = fcntl(socketHandle, F_GETFL, 0);
   if (flags == -1) return SocketStatus::ERROR;
   flags = shouldBlock ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
   return (fcntl(socketHandle, F_SETFL, flags) == 0) ? SocketStatus::OK : SocketStatus::ERROR;
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
    int closeImpl(int socketHandle) {
        return close(socketHandle);
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
            return SocketStatus::ERROR;
        }
    } else {
        return SocketStatus::OK;
    }
}

}