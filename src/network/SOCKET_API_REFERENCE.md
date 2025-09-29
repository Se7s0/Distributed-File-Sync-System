# Socket API Wrapper Reference

## Overview

This document provides a comprehensive reference for the Socket wrapper class in the Distributed File Sync System. The wrapper abstracts platform-specific socket operations and provides a modern C++ interface with RAII (Resource Acquisition Is Initialization) semantics and error handling using the `Result<T>` pattern.

## Table of Contents

1. [Platform Abstractions](#platform-abstractions)
2. [Constructor and Destructor](#constructor-and-destructor)
3. [Socket Creation and Configuration](#socket-creation-and-configuration)
4. [Connection Management](#connection-management)
5. [Data Transfer](#data-transfer)
6. [Socket Options](#socket-options)
7. [Socket Example Analysis](#socket-example-analysis)
8. [Raw Socket API Reference](#raw-socket-api-reference)

---

## Platform Abstractions

### Type Definitions

```cpp
#ifdef DFS_PLATFORM_WINDOWS
    using socket_t = SOCKET;
    constexpr socket_t INVALID_SOCKET_VALUE = INVALID_SOCKET;
#else
    using socket_t = int;
    constexpr socket_t INVALID_SOCKET_VALUE = -1;
#endif
```

**Purpose**: Creates a unified type for socket handles across platforms.
- **Windows**: Uses `SOCKET` type (actually `UINT_PTR`)
- **Linux/Unix**: Uses `int` file descriptor

### Platform Initialization

```cpp
#ifdef DFS_PLATFORM_WINDOWS
    #define close_socket closesocket
    using socklen_t = int;
#else
    #define close_socket ::close
    #include <fcntl.h>
#endif
```

**Purpose**: Maps platform-specific functions to unified names.
- **Windows**: Maps `closesocket` to `close_socket`, defines `socklen_t` as `int`
- **Linux**: Maps standard `close` to `close_socket`, includes `fcntl.h` for non-blocking operations

---

## Constructor and Destructor

### Default Constructor

```cpp
Socket::Socket()
    : socket_(INVALID_SOCKET_VALUE)
    , type_(SocketType::TCP)
    , is_connected_(false) {
    initialize_platform();
}
```

**Function**: Initializes a new Socket object in an invalid state.

**Wrapper Arguments**: None

**Implementation Details**:
- Sets socket handle to invalid value
- Defaults to TCP socket type
- Sets connection state to false
- Calls platform initialization

**Raw Socket API**: No direct API call, just initialization.

### Private Constructor (from native handle)

```cpp
Socket::Socket(socket_t socket)
    : socket_(socket)
    , type_(SocketType::TCP)
    , is_connected_(true) {
}
```

**Function**: Creates Socket object from existing native socket handle (used internally by `accept()`).

**Wrapper Arguments**:
- `socket`: Native socket handle

**Implementation Details**:
- Assumes socket is already connected
- Defaults to TCP type
- Used for accepted client connections

### Destructor

```cpp
Socket::~Socket() {
    close();
}
```

**Function**: RAII cleanup - automatically closes socket when object is destroyed.

**Implementation**: Calls the `close()` method to ensure proper cleanup.

### Move Constructor and Assignment

```cpp
Socket::Socket(Socket&& other) noexcept
    : socket_(other.socket_)
    , type_(other.type_)
    , is_connected_(other.is_connected_) {
    other.socket_ = INVALID_SOCKET_VALUE;
    other.is_connected_ = false;
}
```

**Function**: Enables move semantics for efficient resource transfer.

**Implementation**: Transfers ownership of socket handle and invalidates source object.

---

## Socket Creation and Configuration

### Platform Initialization

```cpp
Result<void> Socket::initialize_platform() {
    if (platform_initialized_) {
        return Ok();
    }

#ifdef DFS_PLATFORM_WINDOWS
    WSADATA wsa_data;
    //You must call WSAStartup() before using any Winsock functions (like socket(), connect(), send(), etc.).
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        return Err<void>(std::string("Failed to initialize Winsock"));
    }
#endif

    platform_initialized_ = true;
    return Ok();
}
```

**Function**: Initializes platform-specific socket libraries.

**Wrapper Arguments**: None (static method)

**Raw Socket API (Windows)**:
- `WSAStartup(MAKEWORD(2, 2), &wsa_data)`
  - `MAKEWORD(2, 2)`: Requests Winsock version 2.2
  - `&wsa_data`: Pointer to `WSADATA` structure to receive details

**Implementation Details**:
- **Windows**: Must call `WSAStartup` before any socket operations
- **Linux**: No initialization required
- Uses static flag to ensure initialization happens only once

### Socket Creation

```cpp
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
```

**Function**: Creates a new socket.

**Wrapper Arguments**:
- `type`: `SocketType::TCP` or `SocketType::UDP`

**Raw Socket API**:
- `::socket(AF_INET, sock_type, protocol)`
  - `AF_INET`: IPv4 address family
  - `sock_type`: `SOCK_STREAM` (TCP) or `SOCK_DGRAM` (UDP)
  - `protocol`: `IPPROTO_TCP` or `IPPROTO_UDP`

**Implementation Details**:
- Validates socket isn't already created
- Maps enum types to raw socket constants
- Stores socket type for later validation
- Returns error if socket creation fails

### Socket Binding

```cpp
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
```

**Function**: Binds socket to a specific address and port.

**Wrapper Arguments**:
- `address`: IP address string (e.g., "127.0.0.1", "0.0.0.0", or empty)
- `port`: Port number (0-65535)

**Raw Socket API**:
- `inet_pton(AF_INET, address.c_str(), &addr.sin_addr)`
  - `AF_INET`: Address family (IPv4)
  - `address.c_str()`: C-string IP address
  - `&addr.sin_addr`: Output buffer for binary address
- `htons(port)`: Converts port from host to network byte order
- `::bind(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))`
  - `socket_`: Socket file descriptor/handle
  - `&addr`: Pointer to address structure (cast to generic `sockaddr*`)
  - `sizeof(addr)`: Size of address structure

**Implementation Details**:
- Validates socket exists
- Handles special case for "0.0.0.0" and empty address (bind to all interfaces)
- Converts string IP to binary format
- Uses network byte order for port
- Comprehensive error handling with context

---

## Connection Management

### Listening for Connections

```cpp
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
```

**Function**: Marks socket as passive, ready to accept connections.

**Wrapper Arguments**:
- `backlog`: Maximum length of pending connections queue (default: 5)

**Raw Socket API**:
- `::listen(socket_, backlog)`
  - `socket_`: Socket file descriptor/handle
  - `backlog`: Maximum number of pending connections

**Implementation Details**:
- Validates socket exists and is TCP type
- Only TCP sockets can listen for connections
- Backlog determines how many connections can be queued while waiting for accept

### Accepting Connections

```cpp
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
```

**Function**: Accepts incoming connection and returns new Socket for client.

**Wrapper Arguments**: None

**Raw Socket API**:
- `::accept(socket_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len)`
  - `socket_`: Listening socket handle
  - `&client_addr`: Output buffer for client address info
  - `&addr_len`: Input/output parameter for address structure size
- `inet_ntop(AF_INET, &client_addr.sin_addr, addr_str, INET_ADDRSTRLEN)`
  - `AF_INET`: Address family
  - `&client_addr.sin_addr`: Binary address to convert
  - `addr_str`: Output buffer for string representation
  - `INET_ADDRSTRLEN`: Buffer size
- `ntohs(client_addr.sin_port)`: Converts port from network to host byte order

**Implementation Details**:
- Blocks until connection arrives (unless socket is non-blocking)
- Returns new Socket object wrapping the client connection
- Extracts and logs client address information
- Uses factory method to create client Socket

### Connecting to Server

```cpp
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
```

**Function**: Establishes connection to remote server.

**Wrapper Arguments**:
- `address`: Server IP address string
- `port`: Server port number

**Raw Socket API**:
- `inet_pton(AF_INET, address.c_str(), &addr.sin_addr)`: Converts IP string to binary
- `htons(port)`: Converts port to network byte order
- `::connect(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))`
  - `socket_`: Client socket handle
  - `&addr`: Server address structure
  - `sizeof(addr)`: Size of address structure

**Implementation Details**:
- Validates socket exists
- Converts address and port to network format
- Blocks until connection established (unless non-blocking)
- Updates internal connection state
- Comprehensive error messages

---

## Data Transfer

### Sending Data

```cpp
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
```

**Function**: Sends data through socket.

**Wrapper Arguments**:
- `data`: Vector of bytes to send

**Raw Socket API**:
- `::send(socket_, reinterpret_cast<const char*>(data.data()), data.size(), 0)`
  - `socket_`: Socket handle
  - `data.data()`: Pointer to data buffer (cast from `uint8_t*` to `char*`)
  - `data.size()`: Number of bytes to send
  - `0`: Flags (none specified)

**Return Value**: Number of bytes actually sent (may be less than requested)

**Implementation Details**:
- Validates socket exists
- Handles type conversion from `std::vector<uint8_t>` to raw buffer
- Returns actual bytes sent (partial sends are possible)
- Error handling for send failures

### Receiving Data

```cpp
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
```

**Function**: Receives data from socket.

**Wrapper Arguments**:
- `max_size`: Maximum bytes to receive

**Raw Socket API**:
- `::recv(socket_, reinterpret_cast<char*>(buffer.data()), max_size, 0)`
  - `socket_`: Socket handle
  - `buffer.data()`: Pointer to receive buffer (cast from `uint8_t*` to `char*`)
  - `max_size`: Maximum bytes to receive
  - `0`: Flags (none specified)

**Return Value**: Vector containing received data, resized to actual bytes received

**Implementation Details**:
- Pre-allocates buffer to maximum size
- Resizes buffer to actual received bytes
- Returns 0 bytes if connection closed by peer
- Move semantics for efficient return

---

## Socket Options

### Non-blocking Mode

```cpp
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
```

**Function**: Enables/disables non-blocking mode.

**Wrapper Arguments**:
- `enable`: true to enable non-blocking, false to disable

**Raw Socket API**:

**Windows**:
- `ioctlsocket(socket_, FIONBIO, &mode)`
  - `socket_`: Socket handle
  - `FIONBIO`: Command to set non-blocking I/O
  - `&mode`: Pointer to mode value (1=non-blocking, 0=blocking)

**Linux**:
- `fcntl(socket_, F_GETFL, 0)`: Get current file status flags
- `fcntl(socket_, F_SETFL, flags)`: Set file status flags
  - `O_NONBLOCK`: Non-blocking flag

**Implementation Details**:
- Platform-specific implementation required
- Non-blocking operations return immediately with error codes like `EAGAIN`/`EWOULDBLOCK`
- Essential for asynchronous I/O patterns

### Address Reuse

```cpp
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
```

**Function**: Enables/disables address reuse.

**Wrapper Arguments**:
- `enable`: true to allow address reuse, false to disable

**Raw Socket API**:
- `setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))`
  - `socket_`: Socket handle
  - `SOL_SOCKET`: Socket-level option
  - `SO_REUSEADDR`: Reuse address option
  - `&opt`: Pointer to option value
  - `sizeof(opt)`: Size of option value

**Implementation Details**:
- Allows binding to address that was recently used
- Prevents "Address already in use" errors during development
- Particularly useful for server applications that restart frequently

### Socket Closure

```cpp
void Socket::close() {
    if (socket_ != INVALID_SOCKET_VALUE) {
        close_socket(socket_);
        socket_ = INVALID_SOCKET_VALUE;
        is_connected_ = false;
        spdlog::debug("Socket closed");
    }
}
```

**Function**: Closes socket and releases resources.

**Raw Socket API**:
- `close_socket(socket_)`: Platform-specific close function
  - **Windows**: `closesocket()`
  - **Linux**: `close()`

**Implementation Details**:
- Safe to call multiple times
- Resets internal state
- Called automatically by destructor (RAII)

---

## Socket Example Analysis

Let's analyze the socket example line by line:

```cpp
#include "dfs/network/socket.hpp"
#include "dfs/core/platform.hpp"
#include <spdlog/spdlog.h>
#include <iostream>
#include <thread>
```
**Purpose**: Include necessary headers for socket operations, platform detection, and logging.

```cpp
spdlog::set_level(spdlog::level::debug);
spdlog::info("Starting socket example on {}", platform_name());
```
**Purpose**: Configure logging and display platform information.
- Sets logging level to debug for detailed output
- `platform_name()` returns "Linux", "Windows", etc.

```cpp
network::Socket server;
```
**Purpose**: Create Socket object using default constructor.
- Socket is initially invalid (`socket_ = INVALID_SOCKET_VALUE`)
- Platform initialization occurs automatically

```cpp
auto result = server.create(network::SocketType::TCP);
if (result.is_error()) {
    spdlog::error("Failed to create server socket: {}", result.error());
    return 1;
}
```
**Purpose**: Create TCP socket and handle potential errors.
- **Raw API called**: `socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)`
- **Error handling**: Uses `Result<T>` pattern for explicit error checking
- **On success**: Socket handle stored in `server.socket_`

```cpp
result = server.set_reuse_address(true);
```
**Purpose**: Enable address reuse to avoid "Address already in use" errors.
- **Raw API called**: `setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))`
- **Effect**: Allows immediate rebinding to same address after program restart

```cpp
result = server.bind("127.0.0.1", 9999);
if (result.is_error()) {
    spdlog::error("Failed to bind: {}", result.error());
    // Try another port
    spdlog::info("Trying alternate port 9998...");
    result = server.bind("127.0.0.1", 9998);
    // ... additional error handling
}
```
**Purpose**: Bind socket to specific address and port, with fallback.
- **Raw API sequence**:
  1. `inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr)` - Convert IP string to binary
  2. `htons(9999)` - Convert port to network byte order
  3. `bind(socket, &addr, sizeof(addr))` - Bind socket to address
- **Error handling**: Graceful degradation with alternate port
- **Address**: "127.0.0.1" (localhost/loopback interface)

```cpp
spdlog::info("Server socket created and bound successfully!");
spdlog::info("Build successful! Basic networking works.");
```
**Purpose**: Confirm successful socket operations.
- Validates that the socket wrapper and underlying APIs work correctly
- Demonstrates basic socket lifecycle: create → configure → bind

---

## Raw Socket API Reference

### Core Socket Functions

| Function | Purpose | Parameters | Return Value |
|----------|---------|------------|--------------|
| `socket()` | Create socket | `domain`, `type`, `protocol` | Socket descriptor or -1/INVALID_SOCKET |
| `bind()` | Bind to address | `sockfd`, `addr`, `addrlen` | 0 on success, -1 on error |
| `listen()` | Mark as passive | `sockfd`, `backlog` | 0 on success, -1 on error |
| `accept()` | Accept connection | `sockfd`, `addr`, `addrlen` | New socket descriptor or -1 |
| `connect()` | Connect to server | `sockfd`, `addr`, `addrlen` | 0 on success, -1 on error |
| `send()` | Send data | `sockfd`, `buf`, `len`, `flags` | Bytes sent or -1 |
| `recv()` | Receive data | `sockfd`, `buf`, `len`, `flags` | Bytes received or -1 |
| `close()`/`closesocket()` | Close socket | `sockfd` | 0 on success, -1 on error |

### Socket Options

| Option | Level | Purpose |
|--------|-------|---------|
| `SO_REUSEADDR` | `SOL_SOCKET` | Allow address reuse |
| `SO_REUSEPORT` | `SOL_SOCKET` | Allow port reuse |
| `SO_KEEPALIVE` | `SOL_SOCKET` | Enable keep-alive |
| `TCP_NODELAY` | `IPPROTO_TCP` | Disable Nagle algorithm |

### Address Structures

```c
struct sockaddr_in {
    short sin_family;        // AF_INET
    unsigned short sin_port; // Port in network byte order
    struct in_addr sin_addr; // IP address
    char sin_zero[8];        // Padding
};
```

### Network Byte Order Functions

| Function | Purpose |
|----------|---------|
| `htons()` | Host to network short (16-bit) |
| `htonl()` | Host to network long (32-bit) |
| `ntohs()` | Network to host short |
| `ntohl()` | Network to host long |
| `inet_pton()` | String to binary address |
| `inet_ntop()` | Binary to string address |

### Error Handling

| Platform | Error Function | Common Errors |
|----------|----------------|---------------|
| Linux | `errno` | `EADDRINUSE`, `ECONNREFUSED`, `EAGAIN` |
| Windows | `WSAGetLastError()` | `WSAEADDRINUSE`, `WSAECONNREFUSED`, `WSAEWOULDBLOCK` |

---

## Key Design Patterns

### 1. RAII (Resource Acquisition Is Initialization)
- Socket automatically closed in destructor
- Prevents resource leaks
- Exception-safe cleanup

### 2. Result<T> Error Handling
- Explicit error checking required
- No hidden exceptions
- Composable error handling

### 3. Move Semantics
- Efficient resource transfer
- Prevents accidental socket copying
- Modern C++ best practices

### 4. Platform Abstraction
- Single interface for Windows/Linux
- Compile-time platform selection
- Zero-cost abstraction

This comprehensive reference covers all aspects of the socket wrapper implementation, providing both high-level understanding and low-level details for effective socket programming.