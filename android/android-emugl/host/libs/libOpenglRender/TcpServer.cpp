#include "TcpServer.h"

#include "TcpSocket.h"

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace lemvr {

// static
TcpServer* TcpServer::createServer(int port) {
    return createServer(port, "127.0.0.1");
}

// static
TcpServer* TcpServer::createServer(int port, const char* ip) {
    int serverHandle = socket(AF_INET, SOCK_STREAM, 0);
    if(serverHandle == 0) {
        return nullptr;
    }

    struct sockaddr_in address;
    unsigned int addrlen = sizeof(address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(ip);
    address.sin_port = htons(port);

    if(bind(serverHandle, (struct sockaddr *)&address, sizeof(address)) < 0) {
        return nullptr;
    }

    if(listen(serverHandle, 3) < 0) {
        return nullptr;
    }

    return new TcpServer(serverHandle, &address, &addrlen);
}

TcpServer::TcpServer(int socketHandle, void* address, unsigned int* addrlen)
    : socketHandle(socketHandle),
      address(address),
      addrlen(addrlen) {}

TcpServer::~TcpServer() {}

namespace {
    int closeImpl(int handle) {
        return close(handle);
    }

    int acceptImpl(int handle, struct sockaddr* address, socklen_t* addrlen) {
        return accept(handle, address, addrlen);
    }
} 

TcpSocket* TcpServer::accept() {
    int handle = acceptImpl(socketHandle, (struct sockaddr*)address, (socklen_t*)addrlen);
    return new TcpSocket(handle);
}

SocketStatus TcpServer::close() {
    return closeImpl(socketHandle) < 0 ? SocketStatus::ERROR : SocketStatus::OK;
}

}