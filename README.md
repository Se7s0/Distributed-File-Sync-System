# Distributed File Sync System - Learning C++ System Design

## Project Overview

This project is a **modular distributed file synchronization service** built in C++ to demonstrate system design principles, cross-platform development, and modern C++ practices. Coming from a .NET background, this guide explains C++ development patterns, build systems, and architectural decisions.

## Table of Contents
- [Why This Architecture](#why-this-architecture)
- [Project Structure](#project-structure)
- [Build System Explained](#build-system-explained)
- [Getting Started](#getting-started)
- [Development Philosophy](#development-philosophy)
- [Core Components](#core-components)
- [Learning Roadmap](#learning-roadmap)

## Why This Architecture

### The .NET vs C++ Development Paradigm

| Aspect | .NET | C++ (This Project) |
|--------|------|-------------------|
| **Build System** | MSBuild/dotnet CLI | CMake (generates native build files) |
| **Package Management** | NuGet (centralized) | FetchContent/vcpkg/Conan (decentralized) |
| **Project Structure** | .csproj files | CMakeLists.txt files |
| **Dependencies** | Runtime references | Compile-time linking |
| **Platform Handling** | CLR abstraction | Preprocessor directives |
| **Memory Management** | Garbage Collection | RAII/Smart Pointers |
| **Error Handling** | Exceptions | Result<T,E> pattern |

### Why These Choices?

1. **CMake over other build systems**
   - Industry standard (used by Google, Microsoft, Facebook)
   - Cross-platform (generates Makefiles on Linux, VS solutions on Windows)
   - Modern dependency management with FetchContent
   - IDE support (VSCode, CLion, Visual Studio)

2. **Modular architecture from the start**
   ```
   src/
   ├── core/       # Platform abstractions, utilities
   ├── network/    # Socket, HTTP implementation
   ├── metadata/   # DDL parser, serialization
   ├── events/     # Event system
   ├── sync/       # Synchronization logic
   ├── server/     # Server application
   └── client/     # Client application
   ```
   Each module has its own CMakeLists.txt, making it independently buildable and testable.

3. **Header/Implementation separation**
   - Headers in `include/dfs/` - public API
   - Implementation in `src/` - private details
   - This is like .NET's internal vs public, but at file level

4. **Result<T,E> pattern instead of exceptions**
   ```cpp
   Result<void> connect(const std::string& address, uint16_t port);
   ```
   - Explicit error handling (can't ignore errors)
   - No hidden control flow (unlike exceptions)
   - Better for systems programming
   - Similar to Rust's Result or .NET's Task<T> without async

## Project Structure

```
cpp_dist_file_proj/
├── CMakeLists.txt           # Root build configuration
├── include/                 # Public headers (API)
│   └── dfs/
│       ├── core/           # Core utilities
│       │   ├── platform.hpp    # OS abstraction
│       │   └── result.hpp      # Error handling
│       └── network/        # Networking
│           └── socket.hpp      # Socket abstraction
├── src/                     # Implementation files
│   ├── core/               # Core module
│   │   └── CMakeLists.txt
│   ├── network/            # Network module
│   │   ├── socket.cpp
│   │   └── CMakeLists.txt
│   └── [other modules]/
├── examples/               # Example applications
│   ├── socket_example.cpp
│   └── CMakeLists.txt
├── tests/                  # Unit tests
├── build/                  # Build output (generated)
└── build.sh               # Build script
```

### Why This Structure?

1. **include/dfs/** - Namespaced headers
   - Prevents header conflicts
   - Clear API boundary
   - Users do `#include "dfs/network/socket.hpp"`

2. **Separate src/ and include/**
   - Hide implementation details
   - Faster compilation (modify .cpp without recompiling dependents)
   - Clear public API

3. **Module CMakeLists.txt**
   - Each module defines its own library
   - Dependencies explicitly declared
   - Can build/test modules independently

## Build System Explained

### CMakeLists.txt Breakdown

```cmake
cmake_minimum_required(VERSION 3.20)
```
- Ensures features we need are available
- CMake 3.20+ has good FetchContent support

```cmake
set(CMAKE_CXX_STANDARD 20)
```
- Uses C++20 (concepts, ranges, coroutines)
- Like targeting .NET 8.0

```cmake
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Werror)
endif()
```
- Platform-specific compiler flags
- Treats warnings as errors (good practice)

```cmake
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.13.0
)
FetchContent_MakeAvailable(spdlog)
```
- Downloads and builds dependencies
- Like NuGet but at compile time
- Ensures exact version match

### Library Types in CMake

```cmake
add_library(dfs_core INTERFACE)    # Header-only library
add_library(dfs_network STATIC)    # Static library (.lib/.a)
add_library(dfs_shared SHARED)     # Dynamic library (.dll/.so)
```

- **INTERFACE**: Header-only, no compilation
- **STATIC**: Compiled into the executable
- **SHARED**: Separate file, loaded at runtime

## Getting Started

### Prerequisites

**Windows (WSL2 recommended):**
```bash
sudo apt update
sudo apt install -y build-essential cmake git
```

**Native Linux:**
```bash
sudo apt install -y build-essential cmake git
```

**macOS:**
```bash
brew install cmake
```

### Building the Project

1. **Clone and enter directory:**
```bash
cd /mnt/c/Users/mohussein/Desktop/swe/cpp_dist_file_proj
```

2. **Build using the script:**
```bash
./build.sh
```

Or manually:
```bash
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . -j4
```

3. **Run the example:**
```bash
./build/examples/socket_example
```

### Build Configurations

**Debug build** (default):
```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
```
- Includes debug symbols
- No optimizations
- Assertions enabled

**Release build**:
```bash
cmake .. -DCMAKE_BUILD_TYPE=Release
```
- Optimizations enabled
- No debug symbols
- Faster execution

**Custom options**:
```bash
cmake .. -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=ON
```

## Development Philosophy

### 1. RAII (Resource Acquisition Is Initialization)

```cpp
class Socket {
    socket_t socket_;
public:
    Socket() : socket_(INVALID_SOCKET_VALUE) {}
    ~Socket() { 
        if (socket_ != INVALID_SOCKET_VALUE) {
            close_socket(socket_);
        }
    }
    // Move semantics
    Socket(Socket&& other) noexcept;
    // Deleted copy (sockets can't be copied)
    Socket(const Socket&) = delete;
};
```

**Why RAII?**
- Automatic resource management (like .NET's `using`)
- No manual cleanup needed
- Exception safe
- Prevents resource leaks

### 2. Platform Abstraction

```cpp
#ifdef _WIN32
    using socket_t = SOCKET;
    #define close_socket closesocket
#else
    using socket_t = int;
    #define close_socket ::close
#endif
```

**Why abstract?**
- Single codebase for Windows/Linux
- Compile-time selection (zero overhead)
- Platform differences hidden from users

### 3. Error Handling Strategy

```cpp
Result<size_t> Socket::send(const std::vector<uint8_t>& data) {
    ssize_t sent = ::send(socket_, data.data(), data.size(), 0);
    if (sent < 0) {
        return Err<size_t>("Failed to send data");
    }
    return Ok(static_cast<size_t>(sent));
}

// Usage
auto result = socket.send(data);
if (result.is_error()) {
    spdlog::error("Send failed: {}", result.error());
    return;
}
size_t bytes_sent = result.value();
```

**Why Result<T,E>?**
- Can't ignore errors (compile-time enforcement possible)
- No hidden control flow
- Composable (can chain operations)
- Performance (no exception overhead)

### 4. Modern C++ Features Used

**Templates** - Generic programming:
```cpp
template<typename T, typename E = std::string>
class Result { ... };
```

**Move semantics** - Efficient resource transfer:
```cpp
Socket(Socket&& other) noexcept;
```

**Smart pointers** - Automatic memory management:
```cpp
Result<std::unique_ptr<Socket>> accept();
```

**Concepts (C++20)** - Constrained templates:
```cpp
template<typename T>
requires std::is_integral_v<T>
void process(T value);
```

## Core Components

### Platform Layer (`include/dfs/core/platform.hpp`)

Provides OS detection and abstraction:
- Compile-time platform detection
- Unified interface for platform-specific features
- Zero-cost abstraction (resolved at compile time)

### Result Type (`include/dfs/core/result.hpp`)

Error handling without exceptions:
- Explicit error propagation
- Type-safe error handling
- Monadic operations (map, and_then - future enhancement)

### Socket Abstraction (`include/dfs/network/socket.hpp`)

Cross-platform networking:
- Handles Windows (Winsock) and Linux (BSD sockets)
- RAII for automatic cleanup
- Non-blocking I/O support
- Move-only semantics (sockets can't be copied)

## Learning Roadmap

### Phase 0: Foundation (Current) ✓
- [x] Project structure
- [x] Build system (CMake)
- [x] Platform abstraction
- [x] Basic socket implementation
- [x] Error handling pattern

**What you learn:**
- How C++ projects are organized
- Build systems and dependency management
- Cross-platform development
- RAII and resource management

### Phase 1: HTTP Implementation
- [ ] HTTP/1.1 parser (state machine)
- [ ] Request/Response objects
- [ ] Connection pooling
- [ ] Async I/O with epoll/IOCP

**What you'll learn:**
- Protocol implementation
- State machines in C++
- Memory-efficient parsing
- Platform-specific async I/O

### Phase 2: Metadata & DDL
- [ ] Custom DDL language design
- [ ] Parser (recursive descent or parser generator)
- [ ] Serialization (binary format)
- [ ] Schema evolution

**What you'll learn:**
- Language design
- Parsing techniques
- Binary protocols
- Backward compatibility

### Phase 3: Event System
- [ ] Publisher-subscriber pattern
- [ ] Event queue with priority
- [ ] Thread-safe event dispatch
- [ ] Component registration

**What you'll learn:**
- Design patterns in C++
- Lock-free programming
- Thread synchronization
- Template metaprogramming

### Phase 4: Sync Engine
- [ ] Merkle trees for diff detection
- [ ] Chunk-based transfer
- [ ] Conflict resolution
- [ ] State machine for sync protocol

**What you'll learn:**
- Distributed systems concepts
- Data structures for sync
- Protocol design
- State management

### Phase 5: OS Integration
- [ ] Windows Minifilter driver
- [ ] Linux inotify/fanotify
- [ ] Service/daemon creation
- [ ] System tray integration

**What you'll learn:**
- Kernel programming basics
- System services
- OS-specific APIs
- User/kernel communication

### Phase 6: Production Features
- [ ] Configuration management
- [ ] Logging and metrics
- [ ] Plugin system
- [ ] Performance optimization

**What you'll learn:**
- Production considerations
- Dynamic loading
- Profiling and optimization
- Maintainable architecture

## Testing Strategy

### Unit Tests (Google Test)
```cpp
TEST(SocketTest, CreateTcpSocket) {
    Socket socket;
    auto result = socket.create(SocketType::TCP);
    EXPECT_TRUE(result.is_ok());
    EXPECT_TRUE(socket.is_valid());
}
```

### Integration Tests
- Test client-server communication
- Test file sync scenarios
- Test error recovery

### Performance Tests
- Measure throughput
- Profile memory usage
- Benchmark against alternatives

## Common Development Tasks

### Adding a New Module

1. Create module directory:
```bash
mkdir -p src/newmodule include/dfs/newmodule
```

2. Create CMakeLists.txt:
```cmake
add_library(dfs_newmodule STATIC
    implementation.cpp
)
target_include_directories(dfs_newmodule PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(dfs_newmodule PUBLIC
    dfs_core
)
```

3. Add to root CMakeLists.txt:
```cmake
add_subdirectory(src/newmodule)
```

### Adding a Dependency

Using FetchContent:
```cmake
FetchContent_Declare(
    libname
    GIT_REPOSITORY https://github.com/org/lib.git
    GIT_TAG v1.0.0
)
FetchContent_MakeAvailable(libname)
```

### Debugging

**With GDB (Linux/WSL):**
```bash
gdb ./build/examples/socket_example
(gdb) break Socket::connect
(gdb) run
(gdb) backtrace
```

**With Visual Studio (Windows):**
- Generate VS solution: `cmake .. -G "Visual Studio 17 2022"`
- Open .sln file
- Set breakpoints and debug

## Comparison with .NET Architecture

| .NET Concept | C++ Equivalent (This Project) |
|--------------|------------------------------|
| `Task<T>` | `Result<T>` (sync) / `std::future<T>` (async) |
| `IDisposable` | RAII destructors |
| `using` statement | RAII scope |
| Interfaces | Pure virtual classes |
| Properties | Getter/setter methods |
| Events | Signal/slot or observer pattern |
| LINQ | Ranges library (C++20) |
| Attributes | Template metaprogramming |
| Reflection | Limited (typeid, type_traits) |
| Garbage Collection | Smart pointers (unique_ptr, shared_ptr) |

## Troubleshooting

### Build Errors

**"CMake not found"**
```bash
sudo apt install cmake
```

**"No CMAKE_CXX_COMPILER"**
```bash
sudo apt install build-essential
```

**Linking errors on Windows**
- Ensure Visual Studio or MinGW is installed
- Use Developer Command Prompt

### Runtime Errors

**"Failed to create socket"**
- Check firewall settings
- Run with appropriate permissions
- Ensure Winsock is initialized (Windows)

**"Address already in use"**
- Previous instance still running
- Wait for TIME_WAIT to expire
- Use SO_REUSEADDR flag

## Next Steps

1. **Run the current example:**
   ```bash
   ./build.sh
   ./build/examples/socket_example
   ```

2. **Experiment with the socket API:**
   - Modify `socket_example.cpp`
   - Try creating a simple echo server
   - Test error handling

3. **Proceed to Phase 1:**
   - Implement HTTP parser
   - Add request/response handling
   - Build a simple HTTP server

## Resources

### C++ Learning
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/)
- [Modern C++ Features](https://github.com/AnthonyCalandra/modern-cpp-features)
- [CMake Tutorial](https://cmake.org/cmake/help/latest/guide/tutorial/)

### System Design
- [High Performance Browser Networking](https://hpbn.co/)
- [Designing Data-Intensive Applications](https://dataintensive.net/)

### Tools
- [Compiler Explorer](https://godbolt.org/) - See assembly output
- [C++ Insights](https://cppinsights.io/) - See template expansions
- [Valgrind](https://valgrind.org/) - Memory debugging (Linux)

## Contributing Guidelines

1. Follow existing code style
2. Add tests for new features
3. Update documentation
4. Ensure cross-platform compatibility
5. Use RAII for resource management
6. Prefer Result<T> over exceptions

---

This project demonstrates modern C++ system design, showing how to build production-grade infrastructure software. Each phase builds upon the previous, creating a complete distributed system from scratch.