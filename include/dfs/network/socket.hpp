#pragma once

#include "dfs/core/platform.hpp"
#include "dfs/core/result.hpp"
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace dfs::network {

#ifdef DFS_PLATFORM_WINDOWS
    using socket_t = SOCKET;
    constexpr socket_t INVALID_SOCKET_VALUE = INVALID_SOCKET;
#else
    using socket_t = int;
    constexpr socket_t INVALID_SOCKET_VALUE = -1;
#endif

// Use standard C++ types instead of POSIX-specific ones
using byte_count_t = std::ptrdiff_t;  // Standard C++ signed size type

enum class SocketType {
    TCP,
    UDP
};

class Socket {
public:
    Socket();
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    Result<void> create(SocketType type);
    Result<void> bind(const std::string& address, uint16_t port);
    Result<void> listen(int backlog = 5);
    Result<std::unique_ptr<Socket>> accept();
    Result<void> connect(const std::string& address, uint16_t port);
    
    Result<size_t> send(const std::vector<uint8_t>& data);
    Result<std::vector<uint8_t>> receive(size_t max_size);
    
    Result<void> set_non_blocking(bool enable);
    Result<void> set_reuse_address(bool enable);
    
    void close();
    bool is_valid() const { return socket_ != INVALID_SOCKET_VALUE; }
    
    socket_t native_handle() const { return socket_; }
    
    static std::unique_ptr<Socket> create_from_native(socket_t socket) {
        return std::unique_ptr<Socket>(new Socket(socket));
    }

private:
    explicit Socket(socket_t socket);
    
    socket_t socket_;
    SocketType type_;
    bool is_connected_;
    
    static bool platform_initialized_;
    static Result<void> initialize_platform();
};

} // namespace dfs::network