# Distributed File Sync System

A production-grade distributed file synchronization system built from scratch in modern C++17. This educational project demonstrates advanced systems programming concepts including network protocols, concurrent programming, event-driven architecture, and distributed systems design.

## Overview

This system enables efficient file synchronization across multiple nodes (clients and servers) using a custom-built HTTP server, metadata management system, event-driven architecture, and intelligent sync engine. The project was built incrementally through 4 major phases, each introducing progressively complex concepts and components.

## Key Features

### Network Layer
- **Custom HTTP/1.1 Server** - Built from scratch with socket programming
  - State machine-based request parsing
  - Multi-threaded connection handling (thread pool with Boost.Asio)
  - Support for multiple server implementations (legacy, thread-pool, async)
  - Flexible routing system with path parameters
  - Binary-safe request/response handling

### Metadata Management
- **Custom DDL (Domain-Specific Language)** for file metadata
  - Lexer and parser implementation (tokenization → AST)
  - Binary serialization for efficient network transfer
  - Thread-safe in-memory metadata store
  - Version tracking and replica management
  - Merkle tree-based diff computation

### Event-Driven Architecture
- **Type-safe Event Bus** using template metaprogramming
  - Publisher-subscriber pattern with type erasure
  - Thread-safe concurrent event dispatch
  - Component-based architecture for loose coupling
  - Event filtering and priority queues
  - Comprehensive event types for all system operations

### Sync Engine
- **Intelligent File Synchronization**
  - Change detection with file hashing
  - Three-way merge conflict resolution
  - Chunked file transfer with integrity checking
  - Session-based sync state management
  - Staging and atomic file updates

### Concurrency & Threading
- **Production-ready concurrency patterns**
  - Reader-writer locks for metadata store
  - Lock-free event queues with condition variables
  - Thread pool for HTTP connection handling
  - Async I/O with Boost.Asio
  - RAII-based resource management

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    Client Application                           │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ HTTP/1.1
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      HTTP Server Layer                          │
│  ┌────────────────┐  ┌────────────────┐  ┌─────────────────┐   │
│  │  HTTP Parser   │→ │  HTTP Router   │→ │  HTTP Server    │   │
│  │  (State Machine)│  │  (Routes)      │  │  (Multi-thread) │   │
│  └────────────────┘  └────────────────┘  └─────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      Event Bus Layer                            │
│  ┌────────────────────────────────────────────────────────┐     │
│  │  Type-Safe Event Bus (Template Metaprogramming)       │     │
│  │  • File Events • Sync Events • System Events          │     │
│  └────────────────────────────────────────────────────────┘     │
│         │                  │                    │                │
│         ▼                  ▼                    ▼                │
│  ┌─────────┐        ┌──────────┐        ┌──────────┐           │
│  │ Logger  │        │  Metrics │        │   Sync   │           │
│  │Component│        │Component │        │ Manager  │           │
│  └─────────┘        └──────────┘        └──────────┘           │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                   Metadata & Sync Engine                        │
│  ┌──────────────┐  ┌─────────────────┐  ┌──────────────────┐   │
│  │   Metadata   │  │  Change         │  │  File Transfer   │   │
│  │   Store      │  │  Detector       │  │  Service         │   │
│  │ (Thread-safe)│  │  (Merkle Tree)  │  │  (Chunked)       │   │
│  └──────────────┘  └─────────────────┘  └──────────────────┘   │
│  ┌──────────────┐  ┌─────────────────┐  ┌──────────────────┐   │
│  │   Conflict   │  │  Sync Session   │  │  DDL Parser      │   │
│  │   Resolver   │  │  (State Machine)│  │  (Lexer/Parser)  │   │
│  └──────────────┘  └─────────────────┘  └──────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
                      ┌───────────────┐
                      │  File System  │
                      └───────────────┘
```

## Development Phases

### Phase 0: Foundation
- Cross-platform socket abstraction (Windows/Linux)
- Error handling with `Result<T>` type (Rust-inspired)
- RAII wrappers for system resources
- Platform-specific abstractions

### Phase 1: HTTP Server & Client (Days 1-5)
**Objective:** Build a working HTTP/1.1 server from scratch

**What was built:**
- HTTP protocol types (methods, headers, status codes)
- State machine-based HTTP parser for incremental request parsing
- HTTP server with connection management
- HTTP router with path parameter support
- Multiple server implementations:
  - Legacy: Single-threaded blocking I/O
  - Thread-pool: Multi-threaded with fixed thread pool
  - Asio: Async I/O with Boost.Asio

**Key learning:**
- Socket programming (TCP, bind, listen, accept)
- HTTP/1.1 protocol implementation
- State machine design for protocol parsing
- Concurrent connection handling
- Thread pool patterns

**Examples:**
- `socket_example.cpp` - Basic TCP client/server
- `http_server_example.cpp` - Simple HTTP server with routes
- `http_server_comparison.cpp` - Performance comparison of implementations
- `http_router_example.cpp` - Advanced routing with parameters

### Phase 2: Metadata & DDL System (Days 6-13)
**Objective:** Create a custom language for file metadata and efficient storage

**What was built:**
- Custom DDL syntax (YAML-like) for file metadata
- Lexer (tokenizer) with indentation handling
- Recursive descent parser
- Binary serialization format with schema versioning
- Thread-safe metadata store with reader-writer locks
- HTTP API endpoints for metadata operations

**Key learning:**
- Language design and parsing theory
- Lexical analysis and tokenization
- Abstract syntax tree construction
- Binary protocol design (endianness, magic numbers)
- Concurrent data structure design
- REST API design

**DDL Format:**
```yaml
file_metadata:
  name: "document.pdf"
  version: 3
  size: 1048576
  hash: "sha256:abc123..."
  last_modified: "2024-01-15T10:30:00Z"
  owner: "laptop1"
  sync_state:
    status: "synced"
    replicas:
      - node: "server1"
        version: 3
```

**Examples:**
- `metadata_server_example.cpp` - Metadata API server
- `metadata_server_asio_example.cpp` - Async metadata server

### Phase 3: Event System (Days 14-19)
**Objective:** Implement event-driven architecture for loose coupling

**What was built:**
- Type-safe event bus using template metaprogramming
- Event types for file operations, sync events, system events
- Thread-safe event queue with condition variables
- Component system for event subscribers
- Logger, metrics, and sync manager components
- Event filtering and priority handling

**Key learning:**
- Observer and publisher-subscriber patterns
- Template metaprogramming and type erasure
- `std::type_index` for runtime type identification
- Condition variables for thread synchronization
- Component-based architecture

**Event Types:**
```cpp
FileAddedEvent         // New file detected
FileModifiedEvent      // File content changed
FileDeletedEvent       // File removed
SyncStartedEvent       // Sync session began
SyncCompletedEvent     // Sync finished successfully
FileConflictDetectedEvent  // Merge conflict found
```

**Examples:**
- `metadata_server_events_example.cpp` - Event-driven metadata server

### Phase 4: Sync Engine (Days 20-26)
**Objective:** Implement intelligent file synchronization with conflict resolution

**What was built:**
- Change detector with file hashing
- Merkle tree for efficient diff computation
- Sync session state machine
- Chunked file transfer with integrity verification
- Conflict detection and resolution strategies
- Sync service control plane
- Complete sync API with upload/download endpoints

**Key learning:**
- Merkle trees for distributed comparison
- State machine design for complex workflows
- Three-way merge algorithms
- Chunked streaming and resume capability
- Conflict resolution strategies (last-write-wins, manual, merge)
- Atomic file operations and staging

**Sync Workflow:**
1. Client registers with server → receives client ID
2. Client starts sync session → receives session ID
3. Client sends local snapshot → server computes diff
4. Server responds with files to upload/download
5. Client uploads files in chunks → server stages them
6. Server finalizes uploads → updates metadata
7. Client downloads modified files
8. Session completes → statistics available

**Examples:**
- `sync_demo_server.cpp` - Complete sync server with all endpoints

## Project Structure

```
Distributed-File-Sync-System/
├── include/dfs/               # Public headers
│   ├── core/                  # Core utilities
│   │   ├── platform.hpp       # Platform abstractions
│   │   └── result.hpp         # Result<T> error handling
│   ├── network/               # Network layer (Phase 1)
│   │   ├── socket.hpp         # Socket abstraction
│   │   ├── http_types.hpp     # HTTP data structures
│   │   ├── http_parser.hpp    # HTTP request parser
│   │   ├── http_router.hpp    # HTTP routing
│   │   ├── http_server.hpp    # Thread-pool server
│   │   ├── http_server_asio.hpp  # Async I/O server
│   │   └── http_server_legacy.hpp # Legacy server
│   ├── metadata/              # Metadata system (Phase 2)
│   │   ├── types.hpp          # Metadata types
│   │   ├── lexer.hpp          # DDL tokenizer
│   │   ├── parser.hpp         # DDL parser
│   │   ├── serializer.hpp     # Binary serialization
│   │   └── store.hpp          # Metadata storage
│   ├── events/                # Event system (Phase 3)
│   │   ├── event_bus.hpp      # Type-safe event bus
│   │   ├── event_queue.hpp    # Thread-safe queue
│   │   ├── events.hpp         # Event type definitions
│   │   └── components.hpp     # Event components
│   └── sync/                  # Sync engine (Phase 4)
│       ├── types.hpp          # Sync types
│       ├── change_detector.hpp # File change detection
│       ├── merkle_tree.hpp    # Merkle diff
│       ├── session.hpp        # Session state machine
│       ├── conflict.hpp       # Conflict resolution
│       ├── transfer.hpp       # File transfer
│       └── service.hpp        # Sync service
├── src/                       # Implementation files
│   ├── network/
│   ├── sync/
│   └── server/
├── examples/                  # Runnable examples
│   ├── socket_example.cpp
│   ├── http_server_example.cpp
│   ├── http_router_example.cpp
│   ├── metadata_server_example.cpp
│   ├── metadata_server_events_example.cpp
│   └── sync_demo_server.cpp
├── tests/                     # Unit and integration tests
│   ├── network/
│   ├── metadata/
│   ├── events/
│   ├── sync/
│   └── e2e/
└── docs/                      # Comprehensive documentation
    ├── phase_1_reference.md
    ├── phase_2_reference.md
    ├── phase_3_reference.md
    └── phase_4_code_reference.md
```

## Technical Highlights

### Advanced C++ Features Used

**Template Metaprogramming:**
```cpp
template<typename EventType>
void EventBus::subscribe(std::function<void(const EventType&)> handler);
// Compiler generates type-safe function for each event type
```

**Type Erasure:**
```cpp
// Store different event handler types in single container
std::unordered_map<std::type_index, std::vector<std::unique_ptr<HandlerBase>>>
```

**RAII (Resource Acquisition Is Initialization):**
```cpp
class Socket {
    ~Socket() { close(); }  // Automatic resource cleanup
};
```

**Move Semantics:**
```cpp
void push(T item) {
    queue_.push(std::move(item));  // Transfer ownership, avoid copies
}
```

**Perfect Forwarding:**
```cpp
template<typename EventType>
void emit(EventType&& event);  // Preserve value category
```

### Concurrency Patterns

**Reader-Writer Locks:**
```cpp
std::shared_mutex mutex_;
std::shared_lock lock(mutex_);   // Multiple readers
std::unique_lock lock(mutex_);   // Exclusive writer
```

**Producer-Consumer Queue:**
```cpp
ThreadSafeQueue<Event> queue_;
producer: queue_.push(event);
consumer: auto event = queue_.pop();  // Blocks until available
```

**Condition Variables:**
```cpp
std::condition_variable cv_;
cv_.wait(lock, []() { return !queue_.empty(); });
cv_.notify_one();
```

**Thread Pool:**
```cpp
// Fixed-size thread pool with work queue
for (size_t i = 0; i < num_threads; ++i) {
    workers_.emplace_back([this] { worker_thread(); });
}
```

### Design Patterns

- **Observer Pattern** - Event bus for decoupled communication
- **State Machine** - HTTP parser, sync session management
- **Factory Pattern** - Server implementation selection
- **Strategy Pattern** - Conflict resolution strategies
- **Component Pattern** - Modular event subscribers
- **Repository Pattern** - Metadata store abstraction

## Building the Project

### Prerequisites

**Linux/WSL:**
```bash
sudo apt install build-essential cmake git libboost-dev
```

**Windows:**
- Visual Studio 2019+ with C++17 support
- CMake 3.15+
- Boost libraries (see BOOST_SETUP_WINDOWS.md)

### Build Steps

```bash
# 1. Clone and navigate
cd /path/to/Distributed-File-Sync-System

# 2. Create build directory
mkdir -p build && cd build

# 3. Configure (automatically downloads spdlog, nlohmann/json, googletest)
cmake .. -DCMAKE_BUILD_TYPE=Release

# 4. Build (parallel with 4 jobs)
cmake --build . -j4

# 5. Run tests
ctest --output-on-failure

# 6. Run examples
./examples/sync_demo_server --port 8080 --data-dir ./data
```

### Build Options

```bash
# Debug build with symbols
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Disable tests
cmake .. -DBUILD_TESTS=OFF

# Disable examples
cmake .. -DBUILD_EXAMPLES=OFF
```

## Running Examples

### 1. Socket Example
```bash
./build/examples/socket_example
# Demonstrates basic TCP client/server communication
```

### 2. HTTP Server Example
```bash
./build/examples/http_server_example 8080
# Visit: http://localhost:8080
# Routes: /, /hello, /info, /echo, /headers
```

### 3. HTTP Router Example
```bash
./build/examples/http_router_example
# Demonstrates advanced routing with path parameters
# Example: GET /users/:id/posts/:post_id
```

### 4. HTTP Server Comparison
```bash
./build/examples/http_server_comparison
# Benchmarks different server implementations
# Outputs requests/sec for legacy, thread-pool, and async variants
```

### 5. Metadata Server Example
```bash
./build/examples/metadata_server_example 8080
# Metadata API at http://localhost:8080/api/metadata
# Test with: curl -X POST http://localhost:8080/api/metadata/add -d @file.ddl
```

### 6. Metadata Server (Async + Events)
```bash
./build/examples/metadata_server_asio_example
# Async I/O version with event-driven logging and metrics
```

### 7. Sync Demo Server (Complete System)
```bash
./build/examples/sync_demo_server --port 8080 --data-dir ./sync_data
# Full sync system with all endpoints

# API endpoints:
# POST /api/register                   - Register client
# POST /api/sync/start                 - Start sync session
# POST /api/sync/diff                  - Compute file differences
# POST /api/file/upload_chunk          - Upload file chunk
# POST /api/file/upload_complete       - Finalize upload
# GET  /api/file/download/<path>       - Download file
# GET  /api/sync/status                - Session status
```

### Testing the Sync Server

```bash
# 1. Register a client
curl -X POST http://localhost:8080/api/register \
  -H "Content-Type: application/json" \
  -d '{"client_name":"laptop1","platform":"linux"}'

# Response: {"client_id":"laptop1-1234567890",...}

# 2. Start sync session
curl -X POST http://localhost:8080/api/sync/start \
  -H "Content-Type: application/json" \
  -d '{"client_id":"laptop1-1234567890"}'

# Response: {"session_id":"abc123","server_snapshot":[...]}

# 3. Get sync status
curl http://localhost:8080/api/sync/status?session_id=abc123

# Response: {"state":"Complete","files_synced":10,...}
```

## API Reference

### Metadata API

**POST /api/metadata/add**
```json
{
  "file_path": "/documents/report.pdf",
  "hash": "sha256:abc123...",
  "size": 1048576,
  "version": 1
}
```

**GET /api/metadata/get/:path**
```json
{
  "file_path": "/documents/report.pdf",
  "hash": "sha256:abc123...",
  "size": 1048576,
  "version": 1,
  "replicas": [...]
}
```

**GET /api/metadata/list**
```json
{
  "files": [
    {"file_path": "/doc1.txt", "hash": "...", "version": 2},
    {"file_path": "/doc2.pdf", "hash": "...", "version": 1}
  ]
}
```

### Sync API

**POST /api/sync/start**
```json
{
  "client_id": "laptop1-123"
}
```
Response:
```json
{
  "session_id": "sess_abc123",
  "server_snapshot": [...]
}
```

**POST /api/sync/diff**
```json
{
  "session_id": "sess_abc123",
  "local_snapshot": [...]
}
```
Response:
```json
{
  "files_to_upload": ["file1.txt", "file2.pdf"],
  "files_to_download": ["file3.doc"],
  "files_to_delete_remote": []
}
```

**POST /api/file/upload_chunk**
```json
{
  "session_id": "sess_abc123",
  "file_path": "/file.txt",
  "chunk_index": 0,
  "total_chunks": 5,
  "data": "48656c6c6f...",  // hex-encoded
  "chunk_hash": "abc123"
}
```

## Testing

### Running Tests

```bash
# Run all tests
cd build
ctest --output-on-failure

# Run specific test suite
./tests/network/http_parser_test
./tests/metadata/parser_test
./tests/events/event_bus_test
./tests/sync/merkle_tree_test
```

### Test Coverage

- **Network Layer:** HTTP parsing, routing, server implementations
- **Metadata System:** Lexer, parser, serialization, store operations
- **Event System:** Event bus, type safety, thread safety, components
- **Sync Engine:** Change detection, Merkle tree, conflict resolution, transfers

### Performance Benchmarks

```bash
# Event bus throughput
./tests/events/event_bus_benchmark
# Expected: 100k-1M+ events/sec

# HTTP server performance
ab -n 10000 -c 100 http://localhost:8080/hello
# Expected: 5k-50k requests/sec (depends on implementation)

# Metadata operations
./tests/metadata/metadata_benchmark
# Expected: 100k+ operations/sec
```

## Key Takeaways

This project demonstrates:

1. **Systems Programming** - Building network protocols from scratch
2. **Concurrent Programming** - Thread-safe data structures and async I/O
3. **Language Design** - Custom DSL with lexer/parser/serializer
4. **Distributed Systems** - Sync protocols, conflict resolution, consistency
5. **Modern C++** - Templates, move semantics, RAII, type erasure
6. **Software Architecture** - Event-driven design, loose coupling, modularity
7. **Production Patterns** - Error handling, logging, metrics, testing

## Future Enhancements

- **Phase 5+:** OS integration for real-time change detection
- **Encryption:** TLS/SSL for network communication
- **Compression:** Gzip/LZ4 for chunk transfers
- **Delta Sync:** rsync-style binary diff
- **Web UI:** Browser-based management interface
- **Clustering:** Multi-server replication
- **Persistence:** Database backend for metadata

## Educational Value

This project was built as a learning exercise to understand:
- How HTTP servers work under the hood
- How to design and implement custom file formats
- Event-driven architecture in C++
- Distributed system challenges (conflicts, consistency)

Each phase built upon the previous, gradually increasing complexity while maintaining clean architecture and comprehensive testing.

## License

This is an educational project. Feel free to use as a reference for learning systems programming and distributed systems concepts.

## Acknowledgments

Built with:
- [Boost](https://www.boost.org/) - Asio for async I/O
- [spdlog](https://github.com/gabime/spdlog) - Fast C++ logging
- [nlohmann/json](https://github.com/nlohmann/json) - JSON for Modern C++
- [GoogleTest](https://github.com/google/googletest) - Unit testing framework

Inspired by real-world systems like Dropbox, Git, and Rsync.
