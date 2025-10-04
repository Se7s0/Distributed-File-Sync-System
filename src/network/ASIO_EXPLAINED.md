# Boost.Asio Event-Driven Server - Line-by-Line Explanation

This document explains **every Asio-specific concept** in `http_server_asio.cpp` and how the event-driven model fundamentally differs from the thread pool approach.

---

## 📋 Table of Contents

1. [Event-Driven vs Thread Pool](#event-driven-vs-thread-pool)
2. [HttpConnection Class - Async Per-Connection Handler](#httpconnection-class)
3. [Shared Pointer Lifetime Management](#shared-pointer-lifetime-management)
4. [Async Read Operation](#async-read-operation)
5. [Async Write Operation](#async-write-operation)
6. [HttpServerAsio Class - Async Accept Loop](#httpserverasio-class)
7. [io_context - The Event Loop](#io_context---the-event-loop)
8. [Complete Flow Diagram](#complete-flow-diagram)
9. [Quick Reference](#quick-reference)

---

## Event-Driven vs Thread Pool

### Thread Pool Model (HttpServer)
```
┌─────────────┐
│ Main Thread │ ──accept()──> blocks until client connects
└─────────────┘
       │
       ├──> Thread 1: read() ──> blocks until data arrives
       ├──> Thread 2: read() ──> blocks until data arrives
       └──> Thread 3: read() ──> blocks until data arrives

🔴 Problem: 1000 slow clients = 1000 blocked threads = slowloris DoS
```

### Event-Driven Model (HttpServerAsio)
```
┌──────────────────┐
│ io_context.run() │ ──> Single thread monitors ALL sockets
└──────────────────┘
       │
       ├── Socket 1: OS notifies when readable
       ├── Socket 2: OS notifies when writable
       ├── Socket 999: OS notifies when readable
       └── Socket 1000: OS notifies when writable

🟢 Solution: 1 thread handles 10,000+ connections via OS notifications
```

**Key difference:**
- **Thread pool**: Threads **block** on I/O (waiting for data)
- **Event-driven**: Threads **react** to OS events (data already available)

---

## HttpConnection Class

### File: `http_server_asio.cpp` Lines 11-15

```cpp
HttpConnection::HttpConnection(tcp::socket socket, HttpRequestHandler handler)
    : socket_(std::move(socket))
    , handler_(std::move(handler))
    , parser_() {
}
```

**What's happening:**
1. **`tcp::socket socket`** - Takes ownership of the connected socket
2. **`std::move(socket)`** - Transfers ownership (sockets cannot be copied)
3. **`handler_(std::move(handler))`** - Stores the user's request handler function
4. **`parser_()`** - Creates a fresh `HttpParser` for this connection

**Why one connection object per client?**
```cpp
// Thread pool: One function handles entire request
void handle_connection(Socket client) {
    while (true) {
        read(); parse(); respond();  // All synchronous
    }
}

// Asio: One object maintains state across async operations
class HttpConnection {
    // State persists between async callbacks
    tcp::socket socket_;
    HttpParser parser_;
    std::array<uint8_t, 8192> buffer_;
};
```

---

## Shared Pointer Lifetime Management

### File: `http_server_asio.cpp` Line 17-19

```cpp
void HttpConnection::start() {
    do_read();
}
```

### File: `http_server_asio.cpp` Line 22

```cpp
void HttpConnection::do_read() {
    auto self = shared_from_this();  // ← KEY CONCEPT!
```

**❓ What is `shared_from_this()`?**

It creates a `shared_ptr` to `this` object, keeping it alive during async operations.

**Why is this needed?**

```cpp
// ❌ WRONG: Connection object gets destroyed too early
void HttpConnection::do_read() {
    socket_.async_read_some(
        asio::buffer(buffer_),
        [this](error_code ec, size_t bytes) {  // ← Captures raw 'this'
            // DANGER: 'this' might be deleted by now!
            parser_.parse(buffer_.data(), bytes);  // ← CRASH!
        }
    );
}  // ← HttpConnection object destroyed here
   // But async operation is still pending!
```

**✅ CORRECT: Keep object alive with shared_ptr**

```cpp
void HttpConnection::do_read() {
    auto self = shared_from_this();  // Creates shared_ptr<HttpConnection>

    socket_.async_read_some(
        asio::buffer(buffer_),
        [this, self](error_code ec, size_t bytes) {  // ← Captures shared_ptr!
            // 'self' keeps HttpConnection alive until lambda exits
            parser_.parse(buffer_.data(), bytes);  // ✅ Safe!
        }
    );
}  // ← HttpConnection NOT destroyed yet (shared_ptr still exists in lambda)
```

**How does `shared_from_this()` work?**

It requires the class to inherit from `std::enable_shared_from_this`:

```cpp
// include/dfs/network/http_server_asio.hpp
class HttpConnection : public std::enable_shared_from_this<HttpConnection> {
    //                      ↑ Enables shared_from_this() method
```

**Lifetime flow:**
1. Server creates: `std::make_shared<HttpConnection>(...)`  → ref_count = 1
2. Lambda captures: `[this, self]` → ref_count = 2
3. Lambda executes: ref_count = 2 (object stays alive)
4. Lambda exits: ref_count = 1
5. Server releases: ref_count = 0 → object destroyed ✅

---

## Async Read Operation

### File: `http_server_asio.cpp` Lines 21-82

```cpp
void HttpConnection::do_read() {
    auto self = shared_from_this();  // Keep alive

    // Async read - returns immediately, callback invoked when data arrives
    socket_.async_read_some(
        asio::buffer(buffer_),
        [this, self](boost::system::error_code ec, size_t bytes_transferred) {
            // ... callback code ...
        }
    );
}
```

### Breaking Down `async_read_some`

**1. What does `async_read_some` do?**

```
Blocking read (thread pool):          Async read (event-driven):
─────────────────────────────          ────────────────────────────
size_t n = socket.read(buffer);        socket_.async_read_some(buffer, callback);
↑ Thread BLOCKS here until             ↑ Returns IMMEDIATELY
  data arrives                           Callback invoked when data ready
```

**2. What is `asio::buffer(buffer_)`?**

```cpp
std::array<uint8_t, 8192> buffer_;  // Member variable

asio::buffer(buffer_)  // Wraps buffer_ for Asio
// Equivalent to: asio::buffer(buffer_.data(), buffer_.size())
```

Asio's buffer abstraction allows it to:
- Know where to write data (`buffer_.data()`)
- Know maximum bytes to read (`buffer_.size()`)
- Prevent buffer overflows

**3. What is the callback signature?**

```cpp
[this, self](boost::system::error_code ec, size_t bytes_transferred)
```

| Parameter | Type | Meaning |
|-----------|------|---------|
| `ec` | `boost::system::error_code` | Did the operation succeed? |
| `bytes_transferred` | `size_t` | How many bytes were read? |

**4. When is the callback invoked?**

The callback is called by `io_context.run()` when:
- ✅ Data arrives on the socket → `ec` is success
- ❌ Socket closes → `ec` = connection reset
- ❌ Timeout occurs → `ec` = timeout
- ❌ Operation cancelled → `ec` = operation_aborted

### Inside the Callback (Lines 28-80)

```cpp
[this, self](boost::system::error_code ec, size_t bytes_transferred) {
    if (!ec) {  // ← Check for errors first
```

**Error handling:**
```cpp
if (!ec) {
    // Success path
} else if (ec != asio::error::operation_aborted) {
    spdlog::debug("Read error: {}", ec.message());
    // operation_aborted = normal shutdown, don't log
}
```

**Parse the data (Lines 33-41):**
```cpp
auto parse_result = parser_.parse(
    buffer_.data(),
    bytes_transferred  // ← Use ACTUAL bytes read, not buffer size
);

if (parse_result.is_error()) {
    handle_error("Parse error: " + parse_result.error());
    return;
}
```

🔄 **Reuses exact same parser from thread pool version!** (No changes needed)

**Check if request is complete (Lines 43-76):**
```cpp
if (parse_result.value()) {  // ← true = request complete
    // ── Handle the request ──
    HttpRequest request = parser_.get_request();

    HttpResponse response;
    try {
        response = handler_(request);  // Call user handler
    } catch (const std::exception& e) {
        // Create 500 error response
    }

    do_write(response);  // Start async write
} else {
    // ── Need more data ──
    do_read();  // Continue reading (recursive)
}
```

**🔄 Recursive async operations:**
```
Client sends: "GET / HTTP/1.1\r\n"  (partial request)
    ↓
do_read() → reads 20 bytes → parser not complete
    ↓
do_read() → reads 30 bytes → parser complete
    ↓
do_write() → sends response
```

Each `do_read()` call registers a NEW async read operation. They don't stack - only one is active at a time.

---

## Async Write Operation

### File: `http_server_asio.cpp` Lines 84-115

```cpp
void HttpConnection::do_write(const HttpResponse& response) {
    auto self = shared_from_this();  // Keep alive during write

    // ── Serialize response (reused from thread pool!) ──
    std::vector<uint8_t> data = response.serialize();

    // ── Store in shared_ptr so it stays alive ──
    auto data_ptr = std::make_shared<std::vector<uint8_t>>(std::move(data));
```

**❓ Why store `data` in a `shared_ptr`?**

```cpp
// ❌ WRONG: Local variable destroyed before async write completes
void do_write(const HttpResponse& response) {
    std::vector<uint8_t> data = response.serialize();

    asio::async_write(socket_, asio::buffer(data), callback);
}  // ← 'data' destroyed here!
   // But async_write is still reading from 'data' → CRASH!

// ✅ CORRECT: shared_ptr keeps data alive
void do_write(const HttpResponse& response) {
    auto data_ptr = std::make_shared<std::vector<uint8_t>>(
        response.serialize()
    );

    asio::async_write(
        socket_,
        asio::buffer(*data_ptr),  // ← Dereference shared_ptr
        [this, self, data_ptr](error_code ec, size_t bytes) {
            // 'data_ptr' keeps vector alive until lambda exits
        }
    );
}
```

**Why `asio::async_write` instead of `async_write_some`?**

| Function | Behavior |
|----------|----------|
| `async_write_some` | Writes SOME bytes (might be partial) |
| `async_write` | Writes ALL bytes (loops internally) |

For HTTP responses, we need to send the complete response, so we use `async_write`.

**The write callback (Lines 100-114):**

```cpp
[this, self, data_ptr](error_code ec, size_t bytes_transferred) {
    if (!ec) {
        spdlog::debug("Sent {} bytes", bytes_transferred);

        // ── Gracefully close connection ──
        boost::system::error_code shutdown_ec;
        socket_.shutdown(tcp::socket::shutdown_both, shutdown_ec);

        // Connection destroyed when lambda exits
        // (no more shared_ptr references to 'self')
    }
}
```

**Socket shutdown:**
```cpp
socket_.shutdown(tcp::socket::shutdown_both, shutdown_ec);
//                                 ↑
//                       Both read and write sides
```

This sends TCP FIN packet to client, signaling no more data.

**Automatic cleanup:**
```cpp
}  // ← Lambda exits
   // ├─ 'self' shared_ptr destroyed → ref_count--
   // └─ If ref_count == 0 → HttpConnection destroyed
```

---

## HttpServerAsio Class

### File: `http_server_asio.cpp` Lines 159-168

```cpp
HttpServerAsio::HttpServerAsio(asio::io_context& io_context, uint16_t port)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
    , io_context_(io_context)
    , port_(port) {

    spdlog::info("HTTP server (Asio event-driven) listening on port {}", port);

    // Start accepting connections
    do_accept();
}
```

**What is `tcp::acceptor`?**

```cpp
tcp::acceptor acceptor_(io_context, tcp::endpoint(tcp::v4(), port));
//            ↑        ↑            ↑
//            name     event loop   bind address (0.0.0.0:port)
```

Similar to `listen()` in thread pool server, but non-blocking:
- Binds to port
- Listens for connections
- **Doesn't block** - uses async accept

**Starting the accept loop:**
```cpp
do_accept();  // Registers first async accept operation
```

### Async Accept Loop (Lines 177-197)

```cpp
void HttpServerAsio::do_accept() {
    // Async accept - returns immediately, callback invoked when client connects
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                spdlog::debug("Accepted new connection (Asio)");

                // ── Create connection handler ──
                std::make_shared<HttpConnection>(
                    std::move(socket),  // Transfer socket ownership
                    handler_            // User's request handler
                )->start();             // Begin async read
            } else {
                spdlog::error("Accept error: {}", ec.message());
            }

            // ── Accept next connection (recursive) ──
            do_accept();  // ← KEY: Keeps server accepting!
        }
    );
}
```

**🔄 How does the recursive accept work?**

```
Server starts:
    do_accept() → registers async accept #1
        ↓
    (returns immediately, server keeps running)
        ↓
Client 1 connects:
    callback invoked → creates HttpConnection #1
                     → do_accept() registers async accept #2
        ↓
Client 2 connects:
    callback invoked → creates HttpConnection #2
                     → do_accept() registers async accept #3
        ↓
... and so on forever
```

**Each `do_accept()` call:**
1. Registers ONE async accept operation
2. Returns immediately
3. Callback invokes `do_accept()` again
4. This creates a never-ending accept loop

**❓ Won't this cause stack overflow?**

No! Each callback is invoked by `io_context.run()`, not directly. The call stack is:

```
do_accept() returns                          ← Stack empty
    ↓
io_context.run() waits for events
    ↓
Client connects → OS notification
    ↓
io_context.run() invokes callback           ← Fresh stack
    ↓
Callback calls do_accept() and returns      ← Stack empty again
```

There's no actual recursion - just a chain of async operations.

---

## io_context - The Event Loop

### File: `http_server_comparison.cpp` Lines 273-284

```cpp
int run_asio_server(uint16_t port) {
    boost::asio::io_context io_context;
    g_io_context = &io_context;

    HttpServerAsio server(io_context, port);
    server.set_handler(handle_request);

    spdlog::info("🟢 Asio server running on http://localhost:{}", port);

    // Run event loop (blocks here)
    io_context.run();  // ← THE MAGIC HAPPENS HERE

    return 0;
}
```

**What is `io_context`?**

`io_context` is the **event loop** - it monitors ALL async operations and invokes callbacks when events occur.

**How does `io_context.run()` work?**

```cpp
io_context.run();  // Blocks and processes events in this loop:

while (io_context has pending operations) {
    1. Ask OS: "Any sockets ready?" (IOCP on Windows, epoll on Linux)
       ↓
    2. OS: "Socket #7 has data to read"
       ↓
    3. io_context invokes corresponding callback
       ↓
    4. Callback executes (do_read, do_write, etc.)
       ↓
    5. Repeat
}
```

**Single-threaded event loop:**
```
One thread runs io_context.run()
    ├── Monitors 10,000 sockets
    ├── Invokes callbacks when events occur
    └── Callbacks execute sequentially (no race conditions!)
```

**How many connections can it handle?**

| Server Type | Max Connections | Limiting Factor |
|-------------|----------------|-----------------|
| Legacy single-threaded | 1 | Only 1 thread |
| Thread pool | ~500 | Number of threads |
| Asio event-driven | 10,000+ | OS limits (file descriptors) |

**CPU usage comparison:**

```
Thread pool with 1000 slow clients:
    Thread 1: read() ──────────────────  [BLOCKED - 0% CPU]
    Thread 2: read() ──────────────────  [BLOCKED - 0% CPU]
    ...
    Thread 1000: read() ────────────────  [BLOCKED - 0% CPU]
    Total: 1000 threads, ~0% CPU (wasteful!)

Asio with 1000 slow clients:
    Main thread: io_context.run() ─────  [WAITING - 0% CPU]
    (OS notifies when data arrives)
    Total: 1 thread, ~0% CPU (efficient!)
```

Both use 0% CPU when idle, but Asio uses **1 thread** instead of **1000 threads**.

**Stopping the event loop:**

```cpp
void signal_handler(int signal) {
    if (signal == SIGINT) {
        if (g_io_context) {
            g_io_context->stop();  // ← Stops io_context.run()
        }
    }
}
```

When `stop()` is called:
1. `io_context.run()` exits
2. All pending async operations are cancelled
3. Callbacks receive `operation_aborted` error
4. Server shuts down cleanly

---

## Complete Flow Diagram

### Single Request Lifecycle

```
┌─────────────────────────────────────────────────────────┐
│ 1. Server Initialization                                │
│    HttpServerAsio server(io_context, 8080);            │
│        ↓                                                │
│    do_accept() → registers async accept                 │
└─────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────┐
│ 2. Main Thread                                          │
│    io_context.run()  ← blocks here, monitoring events   │
└─────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────┐
│ 3. Client Connects                                      │
│    OS: "New connection on port 8080"                    │
│        ↓                                                │
│    io_context invokes accept callback                   │
│        ↓                                                │
│    std::make_shared<HttpConnection>(...)->start()      │
│        ↓                                                │
│    do_read() → registers async read                     │
│        ↓                                                │
│    do_accept() → registers next async accept            │
└─────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────┐
│ 4. Client Sends Data                                    │
│    OS: "Socket #7 has data"                             │
│        ↓                                                │
│    io_context invokes read callback                     │
│        ↓                                                │
│    parser_.parse(buffer_, bytes_transferred)            │
│        ↓                                                │
│    if (complete) → do_write(response)                   │
│    else         → do_read() (more data needed)          │
└─────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────┐
│ 5. Send Response                                        │
│    do_write() → registers async write                   │
│        ↓                                                │
│    OS: "Socket ready to write"                          │
│        ↓                                                │
│    io_context invokes write callback                    │
│        ↓                                                │
│    socket_.shutdown() → close connection                │
│        ↓                                                │
│    HttpConnection destroyed (no more shared_ptrs)       │
└─────────────────────────────────────────────────────────┘
```

### Concurrent Requests

```
Time →

T1: Client A connects
    └─> HttpConnection A created → do_read()

T2: Client B connects (while A is reading)
    └─> HttpConnection B created → do_read()

T3: Client A data arrives
    └─> Callback A invoked → parse → do_write()

T4: Client C connects (while A is writing, B is reading)
    └─> HttpConnection C created → do_read()

T5: Client B data arrives
    └─> Callback B invoked → parse → do_write()

All handled by ONE thread via io_context.run() ✨
```

---

## Quick Reference

### Key Asio Concepts

| Concept | Purpose | Example |
|---------|---------|---------|
| `io_context` | Event loop - monitors all async operations | `io_context.run()` |
| `async_read_some` | Non-blocking read - callback when data arrives | `socket_.async_read_some(buffer, callback)` |
| `async_write` | Non-blocking write - callback when complete | `asio::async_write(socket, buffer, callback)` |
| `async_accept` | Non-blocking accept - callback when client connects | `acceptor_.async_accept(callback)` |
| `shared_from_this()` | Keep object alive during async operations | `auto self = shared_from_this()` |
| `shared_ptr` data | Keep buffer alive during async I/O | `auto data_ptr = std::make_shared<vector<uint8_t>>(data)` |

### Callback Signatures

```cpp
// Read callback
[this, self](boost::system::error_code ec, size_t bytes_transferred) {
    if (!ec) { /* success */ }
}

// Write callback
[this, self, data_ptr](boost::system::error_code ec, size_t bytes_transferred) {
    if (!ec) { /* success */ }
}

// Accept callback
[this](boost::system::error_code ec, tcp::socket socket) {
    if (!ec) { /* new client connected */ }
}
```

### Common Error Codes

| Error Code | Meaning | Handling |
|------------|---------|----------|
| `!ec` (success) | Operation succeeded | Process data |
| `operation_aborted` | Operation cancelled (shutdown) | Don't log, normal |
| `connection_reset` | Client disconnected | Log and cleanup |
| `eof` | End of file (graceful close) | Log and cleanup |

### Memory Safety Patterns

```cpp
// ✅ Keep connection alive
auto self = shared_from_this();

// ✅ Keep buffer alive
auto data_ptr = std::make_shared<std::vector<uint8_t>>(data);

// ✅ Capture both in lambda
[this, self, data_ptr](error_code ec, size_t bytes) {
    // Safe to access 'this->' members
    // Safe to access '*data_ptr'
}
```

### Thread Safety

**Asio guarantees:**
- Callbacks for a single connection are **never concurrent** (run sequentially)
- Different connections' callbacks **may run concurrently** if using multiple threads in `io_context`

**Our single-threaded approach:**
```cpp
io_context.run();  // ONE thread
// → No race conditions between connections!
// → No mutex needed for per-connection state!
```

If we wanted multi-threaded Asio:
```cpp
std::vector<std::thread> threads;
for (int i = 0; i < 4; ++i) {
    threads.emplace_back([&io_context] {
        io_context.run();  // Multiple threads calling run()
    });
}
// → Would need mutex for shared state
// → But still more efficient than thread-per-connection
```

---

## Comparison Table

| Aspect | Thread Pool | Boost.Asio Event-Driven |
|--------|-------------|------------------------|
| **Threading** | N worker threads | 1 event loop thread |
| **Blocking** | `recv()` blocks thread | `async_read_some()` returns immediately |
| **Max connections** | Limited by thread count (~500) | Limited by OS (~10,000+) |
| **Slowloris vulnerable** | Yes (threads block on slow clients) | No (no threads tied to connections) |
| **Memory per connection** | ~1MB (thread stack) | ~10KB (HttpConnection object) |
| **CPU when idle** | 0% (threads blocked in `recv()`) | 0% (thread blocked in `epoll_wait()`) |
| **Code complexity** | Medium (mutex, CV, queue) | Medium (async callbacks, lifetimes) |
| **Code reuse** | 100% parser/response | 100% parser/response |

---

## Summary

**Event-driven programming with Boost.Asio solves the C10K problem** (handling 10,000+ concurrent connections):

1. **No thread-per-connection** - One thread handles all connections
2. **OS notifications** - IOCP/epoll tells us when sockets are ready
3. **Async operations** - `async_read/write/accept` return immediately
4. **Callbacks** - Invoked by `io_context.run()` when events occur
5. **Shared pointers** - Keep objects alive during async operations
6. **Sequential callbacks** - No race conditions per connection

**When to use each:**
- **Legacy single-threaded**: Learning, prototypes
- **Thread pool**: Moderate loads (10-500 concurrent), simpler code
- **Boost.Asio**: High loads (1000+ concurrent), production servers

The beauty of this design: **all three implementations reuse the same `HttpParser` and `HttpResponse` classes** - only the I/O model changes! 🎯
