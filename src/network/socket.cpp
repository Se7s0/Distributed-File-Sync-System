#include "dfs/network/socket.hpp"
#include <spdlog/spdlog.h>
#include <cstring>

#ifdef DFS_PLATFORM_WINDOWS
    #define close_socket closesocket
    using socklen_t = int;
#else
    #define close_socket ::close
    #include <fcntl.h>
#endif

namespace dfs::network {

bool Socket::platform_initialized_ = false;

Result<void> Socket::initialize_platform() {
    if (platform_initialized_) {
        return Ok();
    }
    
#ifdef DFS_PLATFORM_WINDOWS
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        return Err<void>(std::string("Failed to initialize Winsock"));
    }
#endif
    
    platform_initialized_ = true;
    return Ok();
}

Socket::Socket() 
    : socket_(INVALID_SOCKET_VALUE)
    , type_(SocketType::TCP)
    , is_connected_(false) {
    initialize_platform();
}

Socket::Socket(socket_t socket)
    : socket_(socket)
    , type_(SocketType::TCP)
    , is_connected_(true) {
}

Socket::~Socket() {
    close();
}

Socket::Socket(Socket&& other) noexcept
    : socket_(other.socket_)
    , type_(other.type_)
    , is_connected_(other.is_connected_) {
    other.socket_ = INVALID_SOCKET_VALUE;
    other.is_connected_ = false;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        socket_ = other.socket_;
        type_ = other.type_;
        is_connected_ = other.is_connected_;
        other.socket_ = INVALID_SOCKET_VALUE;
        other.is_connected_ = false;
    }
    return *this;
}

Result<void> Socket::create(SocketType type) {
    if (socket_ != INVALID_SOCKET_VALUE) {
        return Err<void>(std::string("Socket already created"));
    }
    
    int sock_type = (type == SocketType::TCP) ? SOCK_STREAM : SOCK_DGRAM;
    int protocol = (type == SocketType::TCP) ? IPPROTO_TCP : IPPROTO_UDP;
    
    socket_ = ::socket(AF_INET, sock_type, protocol);
    if (socket_ == INVALID_SOCKET_VALUE) {
        return Err<void>(std::string("Failed to create socket"));
    }
    
    type_ = type;
    spdlog::debug("Socket created: fd={}, type={}", socket_, 
                  type == SocketType::TCP ? "TCP" : "UDP");
    return Ok();
}

Result<void> Socket::bind(const std::string& address, uint16_t port) {
    if (socket_ == INVALID_SOCKET_VALUE) {
        return Err<void>(std::string("Socket not created"));
    }
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (address == "0.0.0.0" || address.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, address.c_str(), &addr.sin_addr) <= 0) {
            return Err<void>(std::string("Invalid address: ") + address);
        }
    }
    
    if (::bind(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        return Err<void>(std::string("Failed to bind to ") + address + ":" + std::to_string(port));
    }
    
    spdlog::info("Socket bound to {}:{}", address, port);
    return Ok();
}

Result<void> Socket::listen(int backlog) {
    if (socket_ == INVALID_SOCKET_VALUE) {
        return Err<void>(std::string("Socket not created"));
    }
    
    if (type_ != SocketType::TCP) {
        return Err<void>(std::string("Cannot listen on UDP socket"));
    }
    
    if (::listen(socket_, backlog) < 0) {
        return Err<void>(std::string("Failed to listen"));
    }
    
    spdlog::info("Socket listening with backlog={}", backlog);
    return Ok();
}

Result<std::unique_ptr<Socket>> Socket::accept() {
    if (socket_ == INVALID_SOCKET_VALUE) {
        return Err<std::unique_ptr<Socket>>(std::string("Socket not created"));
    }
    
    sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);
    
    socket_t client_socket = ::accept(socket_, 
                                      reinterpret_cast<sockaddr*>(&client_addr), 
                                      &addr_len);
    
    if (client_socket == INVALID_SOCKET_VALUE) {
        return Err<std::unique_ptr<Socket>>(std::string("Failed to accept connection"));
    }
    
    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, addr_str, INET_ADDRSTRLEN);
    spdlog::info("Accepted connection from {}:{}", addr_str, ntohs(client_addr.sin_port));
    
    return Ok(Socket::create_from_native(client_socket));
}

Result<void> Socket::connect(const std::string& address, uint16_t port) {
    if (socket_ == INVALID_SOCKET_VALUE) {
        return Err<void>(std::string("Socket not created"));
    }
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, address.c_str(), &addr.sin_addr) <= 0) {
        return Err<void>(std::string("Invalid address: ") + address);
    }
    
    if (::connect(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        return Err<void>(std::string("Failed to connect to ") + address + ":" + std::to_string(port));
    }
    
    is_connected_ = true;
    spdlog::info("Connected to {}:{}", address, port);
    return Ok();
}

Result<size_t> Socket::send(const std::vector<uint8_t>& data) {
    if (socket_ == INVALID_SOCKET_VALUE) {
        return Err<size_t>(std::string("Socket not created"));
    }
    
    auto sent = ::send(socket_, 
                         reinterpret_cast<const char*>(data.data()), 
                         data.size(), 0);
    
    if (sent < 0) {
        return Err<size_t>(std::string("Failed to send data"));
    }
    
    return Ok(static_cast<size_t>(sent));
}

Result<std::vector<uint8_t>> Socket::receive(size_t max_size) {
    if (socket_ == INVALID_SOCKET_VALUE) {
        return Err<std::vector<uint8_t>>(std::string("Socket not created"));
    }
    
    std::vector<uint8_t> buffer(max_size);
    auto received = ::recv(socket_, 
                             reinterpret_cast<char*>(buffer.data()), 
                             max_size, 0);
    
    if (received < 0) {
        return Err<std::vector<uint8_t>>(std::string("Failed to receive data"));
    }
    
    buffer.resize(received);
    return Ok(std::move(buffer));
}

Result<void> Socket::set_non_blocking(bool enable) {
    if (socket_ == INVALID_SOCKET_VALUE) {
        return Err<void>(std::string("Socket not created"));
    }
    
#ifdef DFS_PLATFORM_WINDOWS
    u_long mode = enable ? 1 : 0;
    if (ioctlsocket(socket_, FIONBIO, &mode) != 0) {
        return Err<void>(std::string("Failed to set non-blocking mode"));
    }
#else
    int flags = fcntl(socket_, F_GETFL, 0);
    if (flags == -1) {
        return Err<void>(std::string("Failed to get socket flags"));
    }
    
    if (enable) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    
    if (fcntl(socket_, F_SETFL, flags) == -1) {
        return Err<void>(std::string("Failed to set non-blocking mode"));
    }
#endif
    
    return Ok();
}

Result<void> Socket::set_reuse_address(bool enable) {
    if (socket_ == INVALID_SOCKET_VALUE) {
        return Err<void>(std::string("Socket not created"));
    }
    
    int opt = enable ? 1 : 0;
    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, 
                   reinterpret_cast<const char*>(&opt), sizeof(opt)) < 0) {
        return Err<void>(std::string("Failed to set SO_REUSEADDR"));
    }
    
    return Ok();
}

void Socket::close() {
    if (socket_ != INVALID_SOCKET_VALUE) {
        close_socket(socket_);
        socket_ = INVALID_SOCKET_VALUE;
        is_connected_ = false;
        spdlog::debug("Socket closed");
    }
}

} // namespace dfs::network