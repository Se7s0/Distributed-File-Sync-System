# Development Guide - Phase-by-Phase Learning Path

## Overview
This guide walks you through each development phase, explaining what you'll build, why, and how to test it. Each phase has working code you can run and experiment with.

---

## 📦 Phase 0: Foundation (Current Phase)
**Duration:** 1-2 days | **Difficulty:** Easy

### What We're Building
A basic project skeleton with cross-platform socket abstraction that compiles and runs on both Windows and Linux.

### Why Start Here?
- Get familiar with C++ build system (CMake)
- Understand project structure
- Learn RAII and error handling patterns
- Have immediate working code to test

### Key Concepts to Learn
1. **CMake Build System**
   ```bash
   mkdir build && cd build
   cmake ..              # Configure
   cmake --build .       # Compile
   ./examples/socket_example  # Run
   ```

2. **RAII Pattern**
   ```cpp
   // Socket automatically closes when it goes out of scope
   {
       Socket socket;
       socket.create(SocketType::TCP);
   } // Destructor called here, socket closed
   ```

3. **Result Pattern**
   ```cpp
   auto result = socket.bind("127.0.0.1", 8080);
   if (result.is_error()) {
       // Handle error explicitly
   }
   ```

### Testing Phase 0
```bash
# Build and run basic test
./build.sh
./build/examples/socket_example

# Expected output:
# [info] Starting socket example on Linux
# [info] Server socket created and bound to port 8080
# [info] Build successful! Basic networking works.
```

### Exercises to Try
1. Modify `socket_example.cpp` to create a UDP socket
2. Try binding to different ports
3. Add error handling for port already in use
4. Create two sockets and make them communicate

### Checkpoint ✓
- [ ] Project builds without errors
- [ ] Socket example runs
- [ ] Understand CMakeLists.txt structure
- [ ] Can modify and rebuild code

---

## 🌐 Phase 1: HTTP Server & Client
**Duration:** 3-5 days | **Difficulty:** Medium

### What We're Building
A working HTTP/1.1 server that can:
- Parse HTTP requests
- Send HTTP responses  
- Handle multiple connections
- Serve static files

### Why HTTP?
- Real protocol implementation
- Learn state machines
- Understand parsing in C++
- Foundation for control plane

### Implementation Steps

#### Step 1.1: HTTP Parser State Machine
```cpp
// include/dfs/network/http_parser.hpp
enum class ParseState {
    METHOD,
    URL,
    VERSION,
    HEADER_NAME,
    HEADER_VALUE,
    BODY,
    COMPLETE
};

class HttpParser {
    ParseState state_;
    HttpRequest request_;
public:
    Result<bool> parse(const char* data, size_t len);
};
```

#### Step 1.2: HTTP Request/Response Objects
```cpp
struct HttpRequest {
    std::string method;
    std::string url;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::vector<uint8_t> body;
};

struct HttpResponse {
    int status_code;
    std::string reason;
    std::unordered_map<std::string, std::string> headers;
    std::vector<uint8_t> body;
    
    std::vector<uint8_t> serialize() const;
};
```

#### Step 1.3: Simple HTTP Server
```cpp
class HttpServer {
    Socket listener_;
    std::function<HttpResponse(const HttpRequest&)> handler_;
public:
    Result<void> listen(uint16_t port);
    Result<void> serve_forever();
};
```

### Testing Phase 1
```bash
# Terminal 1: Start server
./build/examples/http_server

# Terminal 2: Test with curl
curl http://localhost:8080/
curl -X POST http://localhost:8080/api -d "test data"

# Terminal 3: Performance test
ab -n 1000 -c 10 http://localhost:8080/
```

### What You'll Learn
- **State Machines:** How to parse protocols byte by byte
- **Buffer Management:** Efficient string/data handling
- **Async I/O Basics:** Handle multiple connections
- **Protocol Compliance:** Read and implement RFC specs

### Exercises
1. Add support for chunked transfer encoding
2. Implement keep-alive connections
3. Add request routing (URL pattern matching)
4. Build simple REST API endpoints

### Checkpoint ✓
- [ ] Can parse valid HTTP requests
- [ ] Server responds to browser requests
- [ ] Handles concurrent connections
- [ ] Passes basic HTTP compliance tests

---

## 📝 Phase 2: Metadata & DDL System
**Duration:** 4-6 days | **Difficulty:** Medium-Hard

### What We're Building
A domain-specific language (DDL) for describing file metadata and a parser to process it.

### DDL Example
```yaml
file_metadata:
  name: "document.pdf"
  version: 3
  size: 1048576
  hash: "sha256:abc123..."
  permissions:
    owner: "rw"
    group: "r"
    other: "-"
  sync_state:
    status: "synced"
    last_modified: "2024-01-15T10:30:00Z"
    replicas:
      - node: "server1"
        version: 3
      - node: "server2"
        version: 2
```

### Implementation Steps

#### Step 2.1: Lexer (Tokenizer)
```cpp
enum class TokenType {
    IDENTIFIER,
    STRING,
    NUMBER,
    COLON,
    DASH,
    NEWLINE,
    INDENT,
    DEDENT
};

class Lexer {
    std::string input_;
    size_t position_;
public:
    Result<Token> next_token();
};
```

#### Step 2.2: Parser (Recursive Descent)
```cpp
class DDLParser {
    Lexer lexer_;
    Token current_token_;
public:
    Result<FileMetadata> parse_metadata();
private:
    Result<void> expect(TokenType type);
    Result<std::string> parse_string();
    Result<int> parse_number();
};
```

#### Step 2.3: Binary Serialization
```cpp
class MetadataSerializer {
public:
    // Serialize to custom binary format
    std::vector<uint8_t> serialize(const FileMetadata& meta);
    Result<FileMetadata> deserialize(const std::vector<uint8_t>& data);
    
    // Schema version for backward compatibility
    static constexpr uint32_t SCHEMA_VERSION = 1;
};
```

### Testing Phase 2
```cpp
// Test parser
TEST(DDLParser, ParsesValidMetadata) {
    std::string ddl = R"(
        file_metadata:
          name: "test.txt"
          version: 1
    )";
    
    DDLParser parser(ddl);
    auto result = parser.parse_metadata();
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().name, "test.txt");
}

// Test serialization round-trip
TEST(Serializer, RoundTrip) {
    FileMetadata original = create_test_metadata();
    auto serialized = serializer.serialize(original);
    auto deserialized = serializer.deserialize(serialized);
    ASSERT_TRUE(deserialized.is_ok());
    EXPECT_EQ(original, deserialized.value());
}
```

### What You'll Learn
- **Lexical Analysis:** Breaking input into tokens
- **Parsing Techniques:** Recursive descent, error recovery
- **AST Construction:** Building data structures from text
- **Binary Protocols:** Efficient serialization
- **Schema Evolution:** Handling version changes

### Exercises
1. Add support for arrays and nested objects
2. Implement schema validation
3. Add compression to serialization
4. Create a DDL validator tool

### Checkpoint ✓
- [ ] Can parse DDL files without errors
- [ ] Serialization preserves all data
- [ ] Binary format is more compact than text
- [ ] Can evolve schema without breaking old data

---

## 🎯 Phase 3: Event System (ECS-Inspired)
**Duration:** 3-4 days | **Difficulty:** Medium

### What We're Building
An event-driven architecture where components subscribe to and emit events for file system changes.

### Architecture
```cpp
// Event types
struct FileCreatedEvent {
    std::string path;
    size_t size;
    std::chrono::system_clock::time_point timestamp;
};

struct FileModifiedEvent {
    std::string path;
    std::vector<uint8_t> hash;
    size_t new_size;
};

// Event dispatcher
class EventDispatcher {
    template<typename EventType>
    using Handler = std::function<void(const EventType&)>;
    
public:
    template<typename EventType>
    void subscribe(Handler<EventType> handler);
    
    template<typename EventType>
    void emit(const EventType& event);
};
```

### Implementation Steps

#### Step 3.1: Type-Safe Event System
```cpp
class EventBus {
    // Type erasure for storing different event handlers
    struct HandlerBase {
        virtual ~HandlerBase() = default;
    };
    
    template<typename T>
    struct HandlerImpl : HandlerBase {
        std::function<void(const T&)> func;
    };
    
    std::unordered_map<std::type_index, 
                       std::vector<std::unique_ptr<HandlerBase>>> handlers_;
};
```

#### Step 3.2: Component System
```cpp
class Component {
public:
    virtual void on_file_created(const FileCreatedEvent& e) {}
    virtual void on_file_modified(const FileModifiedEvent& e) {}
    virtual void on_file_deleted(const FileDeletedEvent& e) {}
};

class SyncManager : public Component {
    void on_file_modified(const FileModifiedEvent& e) override {
        // Queue file for synchronization
        sync_queue_.push(e.path);
    }
};
```

#### Step 3.3: Thread-Safe Event Queue
```cpp
template<typename T>
class ThreadSafeQueue {
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
public:
    void push(T item);
    std::optional<T> pop();
    std::optional<T> try_pop();
};
```

### Testing Phase 3
```cpp
TEST(EventSystem, SubscribeAndEmit) {
    EventDispatcher dispatcher;
    bool event_received = false;
    
    dispatcher.subscribe<FileCreatedEvent>(
        [&](const FileCreatedEvent& e) {
            event_received = true;
            EXPECT_EQ(e.path, "/test/file.txt");
        });
    
    dispatcher.emit(FileCreatedEvent{"/test/file.txt", 1024});
    EXPECT_TRUE(event_received);
}

// Benchmark event throughput
TEST(EventSystem, Performance) {
    EventDispatcher dispatcher;
    std::atomic<int> counter{0};
    
    dispatcher.subscribe<FileModifiedEvent>(
        [&](const auto& e) { counter++; });
    
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000000; ++i) {
        dispatcher.emit(FileModifiedEvent{});
    }
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Events/sec: " << (1000000.0 / duration.count() * 1000) << "\n";
}
```

### What You'll Learn
- **Template Metaprogramming:** Type-safe generic code
- **Type Erasure:** Storing different types uniformly
- **Observer Pattern:** Decoupled communication
- **Thread Safety:** Mutexes, condition variables
- **Lock-Free Programming:** Optional advanced topic

### Exercises
1. Add event priority system
2. Implement event filtering
3. Add async event handling
4. Create event replay/logging system

### Checkpoint ✓
- [ ] Events flow between components
- [ ] Thread-safe event handling
- [ ] Can handle 100k+ events/second
- [ ] No memory leaks (test with valgrind)

---

## 🔄 Phase 4: Sync Engine & Control Plane
**Duration:** 5-7 days | **Difficulty:** Hard

### What We're Building
The core synchronization logic that detects changes, transfers files, and resolves conflicts.

### Architecture Overview
```
┌─────────────┐         ┌─────────────┐
│   Client    │◄────────►   Server    │
│             │  HTTP    │             │
│  ┌───────┐  │         │  ┌───────┐  │
│  │ Sync  │  │         │  │ Sync  │  │
│  │Engine │  │         │  │Engine │  │
│  └───┬───┘  │         │  └───┬───┘  │
│      │      │         │      │      │
│  ┌───▼───┐  │         │  ┌───▼───┐  │
│  │ Meta  │  │         │  │ Meta  │  │
│  │ Store │  │         │  │ Store │  │
│  └───────┘  │         │  └───────┘  │
└─────────────┘         └─────────────┘
```

### Implementation Steps

#### Step 4.1: File Change Detection
```cpp
class ChangeDetector {
    std::unordered_map<std::string, FileMetadata> known_files_;
public:
    std::vector<FileChange> scan_directory(const std::string& path);
    bool has_file_changed(const std::string& path, const FileMetadata& meta);
};

struct FileChange {
    enum Type { Created, Modified, Deleted };
    Type type;
    std::string path;
    FileMetadata metadata;
};
```

#### Step 4.2: Merkle Tree for Efficient Diff
```cpp
class MerkleTree {
    struct Node {
        std::string hash;
        std::vector<std::unique_ptr<Node>> children;
    };
    std::unique_ptr<Node> root_;
public:
    void build(const std::vector<FileMetadata>& files);
    std::vector<std::string> diff(const MerkleTree& other);
};
```

#### Step 4.3: Sync Protocol State Machine
```cpp
enum class SyncState {
    IDLE,
    COMPUTING_DIFF,
    REQUESTING_METADATA,
    TRANSFERRING_FILES,
    RESOLVING_CONFLICTS,
    APPLYING_CHANGES,
    COMPLETE
};

class SyncSession {
    SyncState state_;
    std::string session_id_;
public:
    Result<void> start_sync();
    Result<void> handle_message(const SyncMessage& msg);
private:
    void transition_to(SyncState new_state);
};
```

#### Step 4.4: Chunk-Based File Transfer
```cpp
class FileTransfer {
    static constexpr size_t CHUNK_SIZE = 64 * 1024; // 64KB chunks
    
public:
    Result<void> send_file(Socket& socket, const std::string& path);
    Result<void> receive_file(Socket& socket, const std::string& path);
    
private:
    std::vector<uint8_t> read_chunk(std::ifstream& file, size_t size);
    Result<void> write_chunk(std::ofstream& file, const std::vector<uint8_t>& data);
};
```

#### Step 4.5: Conflict Resolution
```cpp
class ConflictResolver {
public:
    enum class Strategy {
        LAST_WRITE_WINS,
        MANUAL,
        MERGE
    };
    
    Result<FileMetadata> resolve(
        const FileMetadata& local,
        const FileMetadata& remote,
        Strategy strategy);
};
```

### HTTP Control Plane Endpoints
```cpp
// Server endpoints
POST   /api/register     // Register new client node
POST   /api/sync/start   // Begin sync session
GET    /api/sync/status  // Get sync status
POST   /api/sync/diff    // Send/receive diff
POST   /api/file/upload  // Upload file chunk
GET    /api/file/download// Download file chunk
POST   /api/conflict     // Report/resolve conflict
```

### Testing Phase 4

#### Integration Test: Full Sync Cycle
```cpp
TEST(SyncEngine, FullSyncCycle) {
    // Setup
    TestServer server(8080);
    TestClient client1("client1");
    TestClient client2("client2");
    
    // Client 1 creates file
    client1.create_file("test.txt", "Hello World");
    
    // Sync client1 -> server
    ASSERT_TRUE(client1.sync_with(server).is_ok());
    
    // Sync server -> client2
    ASSERT_TRUE(client2.sync_with(server).is_ok());
    
    // Verify file exists on client2
    EXPECT_TRUE(client2.file_exists("test.txt"));
    EXPECT_EQ(client2.read_file("test.txt"), "Hello World");
}
```

#### Performance Test
```bash
# Create test files
for i in {1..1000}; do
    dd if=/dev/urandom of=testdir/file$i.bin bs=1M count=1
done

# Start server
./build/src/server/dfs_server --port 8080

# Sync client
time ./build/src/client/dfs_client sync --server localhost:8080 --dir testdir

# Expected: ~1000 files (1GB) synced in < 30 seconds on local network
```

### What You'll Learn
- **Distributed Systems:** State synchronization, consistency
- **Protocol Design:** Message formats, state machines
- **Data Structures:** Merkle trees, hash maps
- **Concurrency:** Parallel file transfers
- **Network Programming:** Chunked transfers, flow control

### Exercises
1. Add compression to file transfers
2. Implement delta sync (only transfer changes)
3. Add encryption for secure transfers
4. Build bandwidth throttling
5. Create sync progress reporting

### Checkpoint ✓
- [ ] Can sync 1000 files between nodes
- [ ] Handles network interruptions gracefully
- [ ] Resolves conflicts correctly
- [ ] Efficient diff computation (<1s for 10k files)
- [ ] Parallel transfers improve speed

---

## 🔧 Phase 5: OS Integration
**Duration:** 7-10 days | **Difficulty:** Very Hard

### What We're Building
Native OS integration to detect file changes in real-time without polling.

### Platform-Specific Implementation

#### Linux: inotify Integration
```cpp
class LinuxFileWatcher {
    int inotify_fd_;
    std::unordered_map<int, std::string> watch_descriptors_;
public:
    Result<void> watch_directory(const std::string& path);
    Result<std::vector<FileEvent>> poll_events();
};

// Implementation
Result<void> LinuxFileWatcher::watch_directory(const std::string& path) {
    int wd = inotify_add_watch(inotify_fd_, path.c_str(),
        IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO);
    if (wd < 0) {
        return Err<void>("Failed to add watch");
    }
    watch_descriptors_[wd] = path;
    return Ok();
}
```

#### Windows: Directory Change Notification
```cpp
class WindowsFileWatcher {
    HANDLE dir_handle_;
    OVERLAPPED overlapped_;
    char buffer_[4096];
public:
    Result<void> watch_directory(const std::string& path);
    Result<std::vector<FileEvent>> get_changes();
};

// Using ReadDirectoryChangesW
Result<void> WindowsFileWatcher::watch_directory(const std::string& path) {
    dir_handle_ = CreateFileA(path.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL);
    // Setup async notification...
}
```

#### Advanced: Windows Minifilter Driver (Optional)
```c
// minifilter.c - Kernel driver for Windows
NTSTATUS MiniFilterPreCreate(
    PFLT_CALLBACK_DATA Data,
    PCFLT_RELATED_OBJECTS FltObjects,
    PVOID* CompletionContext)
{
    // Intercept file creation
    UNICODE_STRING fileName;
    FltGetFileNameInformation(Data, &fileName);
    
    // Send to user-mode service
    FltSendMessage(FilterHandle, &ClientPort, 
                   &fileName, sizeof(fileName),
                   NULL, NULL, NULL);
    
    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}
```

### Service/Daemon Creation

#### Linux Systemd Service
```ini
# /etc/systemd/system/dfs.service
[Unit]
Description=Distributed File Sync Service
After=network.target

[Service]
Type=notify
ExecStart=/usr/local/bin/dfs_daemon
Restart=always
User=dfs
Group=dfs

[Install]
WantedBy=multi-user.target
```

#### Windows Service
```cpp
class WindowsService {
    SERVICE_STATUS_HANDLE status_handle_;
public:
    void Run() {
        status_handle_ = RegisterServiceCtrlHandler(
            "DFSService", ServiceCtrlHandler);
        
        SetServiceStatus(status_handle_, SERVICE_RUNNING);
        
        // Run main service loop
        RunSyncEngine();
    }
};
```

### Testing Phase 5
```bash
# Linux: Test inotify
./build/examples/file_watcher /tmp/test_dir &
touch /tmp/test_dir/new_file.txt
echo "content" > /tmp/test_dir/new_file.txt
rm /tmp/test_dir/new_file.txt

# Windows: Test service
sc create DFSService binPath= "C:\dfs\dfs_service.exe"
sc start DFSService
sc query DFSService
```

### What You'll Learn
- **System Programming:** Kernel/user communication
- **Async I/O:** epoll, IOCP, overlapped I/O
- **Service Management:** systemd, Windows services
- **Driver Development:** Basic kernel programming
- **Security:** Privilege management

### Exercises
1. Add recursive directory watching
2. Implement file access auditing
3. Create tray icon with notifications
4. Add performance counters
5. Build auto-start on boot

### Checkpoint ✓
- [ ] Real-time file change detection works
- [ ] Service runs in background
- [ ] Survives system reboot
- [ ] Low CPU usage when idle (<1%)
- [ ] Handles thousands of file events

---

## 🚀 Phase 6: Production & Polish
**Duration:** 5-7 days | **Difficulty:** Medium

### What We're Building
Production-ready features: monitoring, configuration, plugins, and optimization.

### Features to Implement

#### Configuration Management
```cpp
// config.yaml
server:
  address: "0.0.0.0"
  port: 8080
  max_connections: 1000

sync:
  chunk_size: 65536
  max_parallel_transfers: 10
  conflict_strategy: "last_write_wins"

logging:
  level: "info"
  file: "/var/log/dfs/dfs.log"
  max_size: "100MB"
  max_files: 10
```

#### Metrics & Monitoring
```cpp
class MetricsCollector {
    std::atomic<uint64_t> bytes_transferred_{0};
    std::atomic<uint64_t> files_synced_{0};
    std::atomic<uint64_t> conflicts_resolved_{0};
public:
    void record_transfer(size_t bytes);
    void record_sync(const std::string& file);
    
    // Prometheus-compatible output
    std::string export_metrics() const;
};
```

#### Plugin System
```cpp
class Plugin {
public:
    virtual ~Plugin() = default;
    virtual std::string name() const = 0;
    virtual Result<void> initialize() = 0;
    virtual void on_file_synced(const std::string& path) {}
};

class PluginManager {
    std::vector<std::unique_ptr<Plugin>> plugins_;
public:
    Result<void> load_plugin(const std::string& path);
    void notify_file_synced(const std::string& path);
};
```

#### Performance Optimization
```cpp
// Memory pool for frequent allocations
template<typename T>
class ObjectPool {
    std::stack<std::unique_ptr<T>> pool_;
    std::mutex mutex_;
public:
    std::unique_ptr<T> acquire();
    void release(std::unique_ptr<T> obj);
};

// Zero-copy file transfer (Linux sendfile)
Result<size_t> send_file_zero_copy(int out_fd, int in_fd, size_t count) {
    ssize_t sent = sendfile(out_fd, in_fd, nullptr, count);
    if (sent < 0) {
        return Err<size_t>("sendfile failed");
    }
    return Ok(static_cast<size_t>(sent));
}
```

### Testing Phase 6

#### Load Testing
```python
# load_test.py
import concurrent.futures
import requests
import time

def sync_client(client_id):
    """Simulate client sync session"""
    session = requests.Session()
    session.post(f"http://localhost:8080/api/register", 
                 json={"client_id": client_id})
    
    for i in range(100):
        # Upload file
        session.post(f"http://localhost:8080/api/file/upload",
                    files={"file": (f"test_{i}.txt", b"content")})
    
    return client_id

# Run 100 concurrent clients
with concurrent.futures.ThreadPoolExecutor(max_workers=100) as executor:
    futures = [executor.submit(sync_client, i) for i in range(100)]
    results = [f.result() for f in futures]
```

#### Profiling
```bash
# CPU profiling with perf (Linux)
perf record -g ./build/src/server/dfs_server
perf report

# Memory profiling with valgrind
valgrind --leak-check=full --track-origins=yes ./build/src/server/dfs_server

# Thread analysis with helgrind
valgrind --tool=helgrind ./build/src/server/dfs_server
```

### Production Deployment

#### Docker Container
```dockerfile
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y \
    build-essential cmake
COPY . /app
WORKDIR /app
RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    cmake --build .
EXPOSE 8080
CMD ["./build/src/server/dfs_server"]
```

#### CI/CD Pipeline (GitHub Actions)
```yaml
name: Build and Test
on: [push, pull_request]
jobs:
  build:
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [ubuntu-latest, windows-latest]
    steps:
      - uses: actions/checkout@v2
      - name: Build
        run: |
          mkdir build && cd build
          cmake .. -DCMAKE_BUILD_TYPE=Release
          cmake --build .
      - name: Test
        run: cd build && ctest --verbose
```

### What You'll Learn
- **Production Engineering:** Monitoring, logging, metrics
- **Performance Tuning:** Profiling, optimization
- **Plugin Architecture:** Dynamic loading, interfaces
- **Deployment:** Containers, CI/CD
- **Debugging Tools:** perf, valgrind, sanitizers

### Final Checkpoint ✓
- [ ] Handles 1000+ concurrent connections
- [ ] Memory usage stable over time
- [ ] Configuration hot-reload works
- [ ] Metrics exposed for monitoring
- [ ] Plugins can extend functionality
- [ ] Docker image builds and runs
- [ ] CI/CD pipeline passes

---

## 📊 Testing Strategy Throughout Development

### Unit Testing (Every Phase)
```cpp
// Use Google Test for all unit tests
class SocketTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup before each test
    }
    void TearDown() override {
        // Cleanup after each test
    }
};

TEST_F(SocketTest, ConnectToValidAddress) {
    Socket socket;
    ASSERT_TRUE(socket.create(SocketType::TCP).is_ok());
    ASSERT_TRUE(socket.connect("127.0.0.1", 8080).is_ok());
}
```

### Integration Testing (Phase 3+)
```cpp
// Test multiple components together
TEST(Integration, ClientServerSync) {
    auto server = std::thread([]() {
        Server srv;
        srv.listen(8080);
        srv.accept_connections();
    });
    
    Client client;
    ASSERT_TRUE(client.connect("localhost", 8080).is_ok());
    ASSERT_TRUE(client.sync_file("test.txt").is_ok());
    
    server.join();
}
```

### Stress Testing (Phase 4+)
```bash
# Create many files
for i in {1..10000}; do
    echo "test" > testdir/file_$i.txt
done

# Measure sync time
time ./dfs_client sync --dir testdir

# Monitor resources
htop  # CPU/Memory usage
iotop # Disk I/O
```

### Debugging Techniques

#### Memory Debugging
```bash
# AddressSanitizer (compile time)
cmake .. -DCMAKE_CXX_FLAGS="-fsanitize=address"

# Valgrind (runtime)
valgrind --leak-check=full ./your_program
```

#### Thread Debugging
```bash
# ThreadSanitizer
cmake .. -DCMAKE_CXX_FLAGS="-fsanitize=thread"

# GDB for deadlocks
gdb ./your_program
(gdb) run
# When deadlock occurs, Ctrl+C
(gdb) thread apply all bt
```

---

## 📈 Progress Tracking

### How to Know You're Ready for Next Phase

**Phase 0 → Phase 1:** Can create sockets and handle errors properly
**Phase 1 → Phase 2:** HTTP server serves pages to browser
**Phase 2 → Phase 3:** Can parse and serialize metadata
**Phase 3 → Phase 4:** Events flow between components correctly
**Phase 4 → Phase 5:** Files sync between two machines
**Phase 5 → Phase 6:** Real-time sync works without polling
**Phase 6 → Complete:** Production metrics show good performance

### Common Pitfalls & Solutions

| Problem | Solution |
|---------|----------|
| **Segmentation Fault** | Use AddressSanitizer, check null pointers |
| **Memory Leak** | Use smart pointers, run valgrind |
| **Deadlock** | Avoid nested locks, use lock_guard |
| **Race Condition** | Use atomic operations, proper synchronization |
| **Socket Error** | Check errno/WSAGetLastError, handle EAGAIN |
| **Build Fails** | Check CMake version, missing dependencies |

---

## 🎓 Learning Resources for Each Phase

### Phase 0-1: C++ Basics & Networking
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/)
- [Modern C++ Tutorial](https://changkun.de/modern-cpp/)

### Phase 2: Parsing & Compilers
- [Crafting Interpreters](https://craftinginterpreters.com/)
- [ANTLR Mega Tutorial](https://tomassetti.me/antlr-mega-tutorial/)

### Phase 3: Concurrency
- [C++ Concurrency in Action](https://www.manning.com/books/c-plus-plus-concurrency-in-action)
- [Lock-Free Programming](https://preshing.com/archives/)

### Phase 4: Distributed Systems
- [Distributed Systems for Fun and Profit](http://book.mixu.net/distsys/)
- [High Performance Browser Networking](https://hpbn.co/)

### Phase 5: System Programming
- [Linux System Programming](https://man7.org/tlpi/)
- [Windows System Programming](https://docs.microsoft.com/en-us/windows/)

### Phase 6: Production Engineering
- [Site Reliability Engineering](https://sre.google/books/)
- [The Art of Monitoring](https://artofmonitoring.com/)

---

## Summary

This development guide provides a structured path from basic C++ to production-ready distributed systems. Each phase builds on the previous, with concrete implementations and tests. The key is to:

1. **Start small** - Get something working quickly
2. **Test continuously** - Write tests as you code
3. **Iterate** - Refactor and improve
4. **Profile** - Measure before optimizing
5. **Document** - Keep notes on what you learn

Remember: The goal isn't just to build the system, but to understand the principles behind it. Take time to experiment, break things, and learn from the failures.