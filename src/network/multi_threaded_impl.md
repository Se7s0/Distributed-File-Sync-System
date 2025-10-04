# Multi-threaded & Event-Driven HTTP Server Implementation Guide

## Table of Contents
1. [Current Single-Threaded Architecture](#current-single-threaded-architecture)
2. [Phase 1: Thread Pool Implementation](#phase-1-thread-pool-implementation)
3. [Phase 2: Event-Driven with Boost.Asio](#phase-2-event-driven-with-boostasio)
4. [Comparison & When to Use Each](#comparison--when-to-use-each)

---

## Current Single-Threaded Architecture

### Overview

The current HTTP server (`http_server.cpp`) is single-threaded and uses **blocking I/O**:

```cpp
// Line 57-96: serve_forever()
Result<void> HttpServer::serve_forever() {
    running_ = true;

    while (running_) {
        // BLOCKS here waiting for connection
        auto accept_result = listener_.accept();  // Line 73

        auto client = std::move(accept_result.value());

        // BLOCKS here processing entire request
        handle_connection(std::move(client));  // Line 87
    }

    return Ok();
}
```

### Execution Flow

```
Time ──────────────────────────────────────────────────────>

Thread: [Accept#1]──[Process#1: Read→Parse→Handle→Send]──[Accept#2]──[Process#2...]
                     ↑ Client 2 waits here                  ↑ Client 3 waits here
```

### Key Characteristics

**Blocking Points:**
1. **Line 73:** `listener_.accept()` - Blocks until a client connects
2. **Line 169:** `socket.receive()` in `read_request()` - Blocks waiting for data
3. **Line 215:** `socket.send()` in `send_response()` - Blocks while sending

**What happens when client is slow:**
```cpp
// Line 162-199: read_request()
while (!parser.is_complete()) {
    auto recv_result = socket.receive(BUFFER_SIZE);  // Line 169
    // ↑ If client sends 1 byte per second, thread waits here
    // ↑ All other clients are blocked during this time
}
```

### Limitations

| Issue | Impact | Example |
|-------|--------|---------|
| **Head-of-line blocking** | One slow request blocks all others | Client 1 takes 10s → Client 2 waits 10s |
| **No concurrency** | Only 1 request at a time | 10 requests = sequential processing |
| **Resource underutilization** | CPU idle during I/O | Thread sleeps during `recv()` |
| **Vulnerable to slowloris** | Attacker sends slow requests | 1 slow request = server unresponsive |

---

## Phase 1: Thread Pool Implementation

### Overview

Transform the single-threaded server to handle **multiple concurrent connections** using:
1. **Main acceptor thread** - Accepts connections and enqueues them
2. **Thread pool** - Fixed number of worker threads that process requests
3. **Task queue** - Decouples connection acceptance from request processing
4. **Condition variables** - Efficient thread synchronization (no busy-waiting)

### What Problem Does This Solve?

**Before (Single-threaded):**
- 10 concurrent clients → each waits for previous to finish
- Total time: 10 × 2 seconds = 20 seconds

**After (Thread pool with 4 workers):**
- 10 concurrent clients → first 4 process immediately, next 4 start when slots free
- Total time: ~6 seconds (3 batches of 4)

**Still vulnerable to slowloris?** Yes - if attacker opens 4 slow connections, all threads are occupied.

### Architecture Diagram

```
Main Thread (Acceptor):
├─ listen() on socket
├─ accept() → get client socket
├─ {lock queue_mutex_}
│   ├─ Check queue size < max_queue_size_
│   ├─ Push ClientTask{socket, timestamp}
│   └─ {unlock}
├─ queue_cv_.notify_one() → Wake one worker
└─ Loop back

Worker Thread Pool (N threads):
├─ while(running_):
│   ├─ {unique_lock queue_mutex_}
│   ├─ wait(queue_cv_, []{return !queue.empty() || !running_})
│   ├─ if(!running_) break  ← Shutdown signal
│   ├─ Pop task from queue
│   ├─ {unlock} ← CRITICAL: Unlock before processing!
│   ├─ handle_connection(client_socket)  ← Process without holding lock
│   └─ Loop back
└─ Thread exits
```

## Implementation Components

### 1. New Member Variables

```cpp
// Threading components
std::vector<std::thread> worker_threads_;
std::queue<std::unique_ptr<Socket>> task_queue_;
std::mutex queue_mutex_;
std::condition_variable queue_cv_;

// Configuration
size_t thread_pool_size_;     // Number of worker threads
size_t max_queue_size_;       // Max pending connections

// State management (thread-safe)
std::atomic<bool> running_{false};
std::atomic<size_t> active_connections_{0};
std::atomic<size_t> total_processed_{0};
```

### 2. Modern C++ Features Usage

#### **std::atomic - For Simple Flags and Counters**

**Use cases:**
- `running_` flag - Multiple threads check without mutex overhead
- `active_connections_` - Real-time monitoring counter
- `total_processed_` - Statistics tracking

**Why atomic?**
- Lock-free reads/writes for simple types
- No mutex contention for frequently accessed flags
- Memory ordering guarantees prevent race conditions

```cpp
// Set from main thread, read by workers
running_.store(false, std::memory_order_release);

// Workers check without locks
if (!running_.load(std::memory_order_acquire)) break;
```

#### **std::mutex + std::condition_variable - For Task Queue**

**Use case:** Synchronize access to shared task queue

**Pattern:**
```cpp
// Producer (acceptor thread)
{
    std::lock_guard<std::mutex> lock(queue_mutex_);
    task_queue_.push(std::move(client));
}
queue_cv_.notify_one();

// Consumer (worker thread)
std::unique_lock<std::mutex> lock(queue_mutex_);
queue_cv_.wait(lock, [this]{
    return !task_queue_.empty() || !running_;
});
```

**Why condition variable?**
- Workers sleep when queue is empty (no CPU waste)
- Wakes immediately when task arrives (low latency)
- Handles spurious wakeups with predicate

**Critical:** Always unlock before processing tasks to avoid blocking other threads!

#### **std::shared_mutex - Optional for Read-Heavy State**

**Use case:** If handler function or route table is read frequently but updated rarely

```cpp
std::shared_mutex handler_mutex_;

// Workers (concurrent reads)
std::shared_lock<std::shared_mutex> lock(handler_mutex_);
auto response = handler_(request);

// Main thread (exclusive write)
std::unique_lock<std::shared_mutex> lock(handler_mutex_);
handler_ = new_handler;
```

**When to skip:** If handler is set once at startup, regular member access is fine.

#### **std::future/std::promise - NOT Recommended Here**

**Why avoid for server loop?**
- Creates/destroys future objects per connection (overhead)
- No thread reuse (defeats purpose of thread pool)
- Harder to control thread lifecycle and shutdown

**Where they're useful:**
- One-off async operations (health checks, metrics collection)
- Fire-and-forget tasks that don't need pooling

#### **std::async - Also Skip**

**Problems:**
- Implementation may spawn unbounded threads
- No control over thread pool size
- Can't implement graceful shutdown easily

**Verdict:** Manual thread pool with condition variables gives better control.

### 3. Thread Pool Sizing

**Formula depends on workload type:**

#### CPU-Bound (computation-heavy handlers):
```cpp
thread_pool_size_ = std::thread::hardware_concurrency(); // Usually core count
```

#### I/O-Bound (network/disk operations - typical HTTP server):
```cpp
thread_pool_size_ = std::thread::hardware_concurrency() * 2;  // 2-4x cores
```

**Rationale:**
- I/O-bound threads block on socket operations
- Oversubscription keeps CPU busy while some threads wait
- Too many threads → context switching overhead

**Make it configurable:**
```cpp
HttpServer(size_t num_threads = std::thread::hardware_concurrency() * 2);
```

### 4. Worker Thread Implementation

```cpp
void HttpServer::worker_thread() {
    while (true) {
        std::unique_ptr<Socket> client;

        {
            // Wait for task or shutdown signal
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !task_queue_.empty() || !running_;
            });

            // Check shutdown
            if (!running_ && task_queue_.empty()) {
                break;  // Exit thread
            }

            // Get task
            if (!task_queue_.empty()) {
                client = std::move(task_queue_.front());
                task_queue_.pop();
            }
        }  // ← Lock released here!

        // Process connection WITHOUT holding lock
        if (client) {
            active_connections_.fetch_add(1);
            handle_connection(std::move(client));
            active_connections_.fetch_sub(1);
            total_processed_.fetch_add(1);
        }
    }
}
```

**Key points:**
- Predicate prevents spurious wakeups
- Lock scope limited to queue access only
- Atomic counters track active work

### 5. Modified serve_forever()

```cpp
Result<void> HttpServer::serve_forever() {
    // Validation...

    // Spawn worker threads
    for (size_t i = 0; i < thread_pool_size_; ++i) {
        worker_threads_.emplace_back([this] { worker_thread(); });
    }

    running_ = true;

    // Acceptor loop
    while (running_) {
        auto accept_result = listener_.accept();
        if (accept_result.is_error()) {
            if (!running_) break;
            continue;
        }

        auto client = std::move(accept_result.value());

        // Enqueue with size check
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (task_queue_.size() >= max_queue_size_) {
                spdlog::warn("Queue full, rejecting connection");
                // Send 503 Service Unavailable
                send_overload_response(*client);
                continue;
            }
            task_queue_.push(std::move(client));
        }

        queue_cv_.notify_one();  // Wake one worker
    }

    return Ok();
}
```

### 6. Graceful Shutdown

```cpp
void HttpServer::stop() {
    if (running_) {
        spdlog::info("Stopping server...");

        // 1. Signal shutdown (atomic, no lock needed)
        running_.store(false, std::memory_order_release);

        // 2. Wake ALL workers so they see running_ = false
        queue_cv_.notify_all();

        // 3. Close listener to unblock accept()
        listener_.close();

        // 4. Join all worker threads
        for (auto& thread : worker_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        // 5. Optional: Drain remaining tasks
        // (Or log warning if queue not empty)

        spdlog::info("Server stopped. Processed {} requests",
                     total_processed_.load());
    }
}
```

**Why notify_all()?**
- Workers might be sleeping when `running_` becomes false
- `notify_all()` wakes them immediately to check shutdown condition
- `notify_one()` might wake only one thread, leaving others stuck

### 7. Connection Safety Features

#### Queue Size Limiting
```cpp
max_queue_size_ = 1000;  // Configurable

if (task_queue_.size() >= max_queue_size_) {
    // Reject with HTTP 503
    HttpResponse overload(HttpStatus::SERVICE_UNAVAILABLE);
    overload.set_header("Retry-After", "5");
    send_response(*client, overload);
    client->close();
    continue;
}
```

**Why?** Prevents memory exhaustion under DoS attacks.

#### Socket Timeouts
```cpp
// In handle_connection() before processing
struct timeval timeout;
timeout.tv_sec = 30;   // 30 second timeout
timeout.tv_usec = 0;

setsockopt(client->fd(), SOL_SOCKET, SO_RCVTIMEO,
           &timeout, sizeof(timeout));
```

**Why?** Prevents slow clients (slowloris attacks) from hogging threads indefinitely.

#### Connection Tracking
```cpp
// Monitor active connections
size_t active = active_connections_.load();
if (active > thread_pool_size_ * 0.8) {
    spdlog::warn("High load: {}/{} threads busy",
                 active, thread_pool_size_);
}
```

### 8. What NOT to Over-Engineer (Phase 1)

#### ❌ Lock-Free Queues
- **Complexity:** Hard to implement correctly, subtle bugs
- **Gains:** Marginal for HTTP server workload
- **Verdict:** Use `std::queue` + `std::mutex` (proven, debuggable)

#### ❌ Work-Stealing Schedulers
- **Complexity:** Each thread has own queue, steals from others when idle
- **Gains:** Better load balancing for heterogeneous tasks
- **Verdict:** Overkill for uniform HTTP request handling

#### ❌ std::jthread (C++20)
- **Nice feature:** Auto-joins on destruction, stop tokens built-in
- **Required?** No - manual `join()` in destructor works fine
- **Verdict:** Use if C++20 available, not essential

### 9. Thread Pool Limitations

**What thread pool SOLVES:**
- ✅ Multiple concurrent requests
- ✅ Better CPU utilization
- ✅ Bounded resource usage (fixed thread count)

**What thread pool DOESN'T solve:**
- ❌ Slowloris attacks (slow clients still occupy threads)
- ❌ Scaling beyond ~1000 concurrent connections (thread overhead)
- ❌ Memory efficiency (each thread = ~8MB stack)

**For these problems, you need event-driven I/O (Phase 2).**

---

### 10. Implementation Checklist (Phase 1)

- [ ] Add thread pool member variables to `HttpServer` class
- [ ] Add queue + synchronization primitives (`mutex`, `condition_variable`)
- [ ] Make `running_` an `std::atomic<bool>`
- [ ] Implement `worker_thread()` function
- [ ] Modify `serve_forever()`: spawn workers → loop accept → enqueue
- [ ] Modify `stop()`: set atomic flag → notify all → join threads
- [ ] Add queue size limiting with 503 responses
- [ ] Set socket receive timeouts (`SO_RCVTIMEO`)
- [ ] Add monitoring counters (active connections, total processed)
- [ ] Test with concurrent `curl` requests (Apache Bench, `wrk`)
- [ ] Load test to find optimal thread pool size
- [ ] Document configuration parameters

### 11. Testing Strategy (Phase 1)

#### 1. Correctness
```bash
# Terminal 1: Start server
./http_server

# Terminal 2: Concurrent requests
for i in {1..100}; do
  curl http://localhost:8080/test &
done
wait
```

#### 2. Load Testing
```bash
# Apache Bench
ab -n 10000 -c 100 http://localhost:8080/

# wrk (modern alternative)
wrk -t4 -c100 -d30s http://localhost:8080/
```

#### 3. Stress Testing (Queue Limits)
```bash
# Flood server to trigger 503 responses
ab -n 100000 -c 500 http://localhost:8080/
# Should see "Queue full" warnings in logs
```

### 12. Performance Expectations (Phase 1)

**Before (Single-threaded):**
- Requests/sec: ~100-500 (depends on handler latency)
- Concurrency: 1
- Latency under load: Linearly increases

**After (Thread Pool with N=8):**
- Requests/sec: ~800-4000 (8x improvement if I/O bound)
- Concurrency: 8 simultaneous requests
- Latency under load: Stable until saturation

**Bottleneck shift:** From connection acceptance to queue/thread availability.

---

## Phase 2: Event-Driven with Boost.Asio

### Why Event-Driven I/O?

The thread pool approach (Phase 1) still has a fundamental problem: **threads block on slow I/O**.

**Slowloris attack example:**
```
Attacker opens 8 connections (= thread pool size)
Sends 1 byte every 30 seconds
→ All 8 threads blocked in socket.receive()
→ New legitimate requests get queued/rejected
→ Server effectively down
```

**Event-driven I/O solution:**
- **Don't wait for data** - let the OS notify you when it arrives
- **One thread monitors thousands of sockets** - no thread-per-connection
- **Slow clients cost almost nothing** - just memory for socket state

### Blocking vs Non-Blocking vs Asynchronous I/O

#### Blocking I/O (Current Implementation)
```cpp
// Thread STOPS here until data arrives
auto data = socket.receive();
process(data);
```
**Problem:** Thread is stuck, can't do other work.

#### Non-Blocking I/O
```cpp
socket.set_nonblocking(true);
auto data = socket.receive();  // Returns immediately!

if (data.is_error() && data.error() == EAGAIN) {
    // No data yet - do other work
} else {
    process(data.value());
}
```
**Problem:** You have to constantly poll "is data ready?" (wastes CPU).

#### Asynchronous I/O (Event-Driven)
```cpp
// Start async operation, provide callback
socket.async_receive([](error_code ec, size_t bytes) {
    // OS calls this when data arrives!
    process(data);
});

// Continue immediately - don't wait
do_other_work();
```
**Solution:** OS notifies you when data ready. No waiting, no polling.

### What is Boost.Asio?

**Boost.Asio** = Asynchronous I/O library for C++

**What it provides:**
1. **Cross-platform async I/O** - Uses best mechanism for each OS:
   - Windows: IOCP (I/O Completion Ports)
   - Linux: epoll
   - macOS: kqueue
   - Fallback: select/poll

2. **io_context** - Event loop that drives async operations

3. **Asynchronous operations** - Non-blocking socket I/O with callbacks

4. **Timers, coroutines, strand** - Additional async utilities

**Industry usage:** Production servers (WebSocket++, Beast HTTP, many game servers)

### Core Concepts

#### 1. io_context - The Event Loop

Think of `io_context` as the **operating system's notification system**.

```cpp
boost::asio::io_context io_context;

// Register async operations (they don't block!)
socket.async_read(...);   // "Tell me when data arrives"
timer.async_wait(...);    // "Tell me when 5 seconds pass"

// Run event loop - blocks here processing events
io_context.run();  // Calls callbacks when events occur
```

**How it works internally (Windows):**
```
io_context.run()
  → GetQueuedCompletionStatus(iocp_handle)  // Windows IOCP
  → Waits until ANY registered I/O completes
  → Calls the appropriate callback
  → Loop back
```

**How it works internally (Linux):**
```
io_context.run()
  → epoll_wait(epoll_fd)  // Linux epoll
  → Waits until ANY registered socket has activity
  → Calls the appropriate callback
  → Loop back
```

#### 2. Async Operations - Callbacks

All async operations follow this pattern:
```cpp
async_operation(args..., completion_handler);
```

**Example: Async accept**
```cpp
acceptor.async_accept(socket, [](error_code ec) {
    if (!ec) {
        // New connection accepted!
        // Socket is now ready to use
    }
});
// Returns IMMEDIATELY - doesn't wait for connection
```

**Example: Async receive**
```cpp
socket.async_receive(buffer, [](error_code ec, size_t bytes) {
    if (!ec) {
        // Data received! 'bytes' tells you how much
        process(buffer, bytes);
    }
});
// Returns IMMEDIATELY - doesn't wait for data
```

#### 3. Callback Execution

**Key rule:** Callbacks run in the thread that calls `io_context.run()`

**Single-threaded model:**
```cpp
io_context.run();  // This thread runs ALL callbacks
```

**Multi-threaded model:**
```cpp
std::vector<std::thread> threads;
for (int i = 0; i < 4; i++) {
    threads.emplace_back([&io_context]() {
        io_context.run();  // 4 threads share callback execution
    });
}
```

### Boost.Asio HTTP Server Architecture

```
io_context (Event Loop)
    ↓
Acceptor (listening socket)
    ↓
async_accept() → New connection arrives
    ↓
Create Connection object
    ↓
async_read() → Start reading HTTP request
    ↓
[OS monitors socket, doesn't block thread]
    ↓
Data arrives → Callback invoked
    ↓
Parse HTTP request
    ↓
If incomplete → async_read() again
If complete → Call handler
    ↓
async_write() → Send response
    ↓
[OS sends data asynchronously]
    ↓
Write complete → Close or keep-alive
```

**Key difference from thread pool:**
- **Thread pool:** 1 thread waits per connection
- **Asio:** 1 event loop tracks 10,000 connections

### Implementation Example

#### Step 1: Install Boost

**Windows (vcpkg):**
```bash
vcpkg install boost-asio:x64-windows
```

**Linux:**
```bash
sudo apt-get install libboost-all-dev
```

**CMakeLists.txt:**
```cmake
find_package(Boost REQUIRED COMPONENTS system)
target_link_libraries(http_server PRIVATE Boost::system)
```

#### Step 2: Connection Class

Each connection is an object that manages its own async operations:

```cpp
class HttpConnection : public std::enable_shared_from_this<HttpConnection> {
public:
    HttpConnection(tcp::socket socket, HttpRequestHandler handler)
        : socket_(std::move(socket))
        , handler_(std::move(handler)) {}

    // Start async reading
    void start() {
        do_read();
    }

private:
    void do_read() {
        auto self = shared_from_this();  // Keep connection alive

        // Async read - returns immediately!
        socket_.async_read_some(
            boost::asio::buffer(buffer_),
            [this, self](boost::system::error_code ec, size_t bytes) {
                if (!ec) {
                    // Feed data to parser
                    parser_.parse(buffer_.data(), bytes);

                    if (parser_.is_complete()) {
                        // Request complete - call handler
                        auto response = handler_(parser_.get_request());
                        do_write(response);
                    } else {
                        // Need more data
                        do_read();  // Continue reading
                    }
                }
            }
        );
    }

    void do_write(const HttpResponse& response) {
        auto self = shared_from_this();
        auto data = response.serialize();

        // Async write - returns immediately!
        boost::asio::async_write(
            socket_,
            boost::asio::buffer(data),
            [this, self](boost::system::error_code ec, size_t /*bytes*/) {
                if (!ec) {
                    // Response sent - close connection
                    socket_.shutdown(tcp::socket::shutdown_both, ec);
                }
            }
        );
    }

    tcp::socket socket_;
    HttpRequestHandler handler_;
    HttpParser parser_;
    std::array<char, 8192> buffer_;
};
```

**Why `shared_from_this()`?**
- Callbacks might execute after function returns
- `shared_ptr` keeps connection alive until all callbacks complete

#### Step 3: Async HTTP Server

```cpp
class HttpServerAsio {
public:
    HttpServerAsio(boost::asio::io_context& io_context, uint16_t port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
        , io_context_(io_context) {
        do_accept();
    }

    void set_handler(HttpRequestHandler handler) {
        handler_ = std::move(handler);
    }

private:
    void do_accept() {
        // Async accept - doesn't block!
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec) {
                    // New connection - create handler
                    std::make_shared<HttpConnection>(
                        std::move(socket), handler_
                    )->start();
                }

                // Accept next connection
                do_accept();  // Recursive - keeps accepting
            }
        );
    }

    tcp::acceptor acceptor_;
    boost::asio::io_context& io_context_;
    HttpRequestHandler handler_;
};
```

#### Step 4: Main Function

**Single-threaded event loop:**
```cpp
int main() {
    boost::asio::io_context io_context;

    HttpServerAsio server(io_context, 8080);
    server.set_handler([](const HttpRequest& req) {
        HttpResponse res(HttpStatus::OK);
        res.set_body("Hello from Asio!");
        return res;
    });

    spdlog::info("Server listening on port 8080");

    // Run event loop (blocks here)
    io_context.run();

    return 0;
}
```

**Multi-threaded event loop (for CPU-heavy handlers):**
```cpp
int main() {
    boost::asio::io_context io_context;

    HttpServerAsio server(io_context, 8080);
    server.set_handler([](const HttpRequest& req) {
        // CPU-heavy processing
        return expensive_computation(req);
    });

    // Run with thread pool
    std::vector<std::thread> threads;
    size_t num_threads = std::thread::hardware_concurrency();

    for (size_t i = 0; i < num_threads; i++) {
        threads.emplace_back([&io_context]() {
            io_context.run();  // All threads share event loop
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    return 0;
}
```

### How Asio Handles Slowloris

**Slowloris attack:**
```
Attacker opens 10,000 connections
Sends 1 byte every 30 seconds
```

**Thread pool response:**
- 10,000 threads created (if unbounded pool)
- OR queue fills up and rejects requests (if bounded)
- Memory: 10,000 × 8MB = 80GB!
- Result: **Server crashes or becomes unresponsive**

**Asio response:**
```cpp
// Attacker opens 10,000 connections
for (int i = 0; i < 10000; i++) {
    async_accept([](tcp::socket socket) {
        async_read(socket, [](data) {
            // Waiting for data...
        });
    });
}

// Resource usage:
// - Threads: 1-8 (event loop threads)
// - Memory: 10,000 × ~few KB = ~50MB
// - CPU: Idle (just monitoring sockets)
```

**Legitimate request arrives:**
```cpp
// Client 10,001 connects and sends complete request immediately
async_accept([](tcp::socket socket) {
    async_read(socket, [](data) {
        // Data arrived! epoll/IOCP notifies immediately
        // Process and respond - no delay
    });
});
```

**Result:** Server remains responsive even with 10,000 slow connections!

### Asio + Thread Pool Hybrid (Best of Both Worlds)

For CPU-heavy handlers, combine event-driven I/O with worker threads:

```cpp
class HttpServerHybrid {
public:
    HttpServerHybrid(boost::asio::io_context& io_context, uint16_t port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
        , io_context_(io_context)
        , work_guard_(boost::asio::make_work_guard(worker_context_)) {

        // Spawn worker threads for CPU-heavy tasks
        for (size_t i = 0; i < std::thread::hardware_concurrency(); i++) {
            worker_threads_.emplace_back([this]() {
                worker_context_.run();
            });
        }

        do_accept();
    }

private:
    void handle_request(std::shared_ptr<HttpConnection> conn,
                       const HttpRequest& req) {
        // Offload CPU-heavy work to worker thread pool
        boost::asio::post(worker_context_, [this, conn, req]() {
            // CPU-heavy processing in worker thread
            auto response = handler_(req);

            // Post response back to I/O thread
            boost::asio::post(io_context_, [conn, response]() {
                conn->send_response(response);
            });
        });
    }

    boost::asio::io_context& io_context_;      // I/O event loop
    boost::asio::io_context worker_context_;   // Worker thread pool
    std::vector<std::thread> worker_threads_;
    // ...
};
```

**Architecture:**
```
I/O Threads (1-2):
  → async_accept()
  → async_read()
  → When request complete: post to worker_context_
  → async_write() response

Worker Threads (8-16):
  → Execute CPU-heavy handlers
  → Post response back to I/O thread
```

**Benefits:**
- I/O threads never block (always responsive to new connections)
- Worker threads do CPU-heavy lifting
- Scales to 10,000+ connections with just ~20 threads total

### Boost.Asio Integration Steps

#### 1. Add Asio Headers
```cpp
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
```

#### 2. Create New Server Class
```cpp
// Keep existing HttpServer (thread pool version)
// Add new HttpServerAsio (event-driven version)

class HttpServerAsio {
    // ... as shown above
};
```

#### 3. Modify CMakeLists.txt
```cmake
find_package(Boost REQUIRED COMPONENTS system)

add_library(http_server
    src/network/http_server.cpp          # Thread pool version
    src/network/http_server_asio.cpp     # Asio version
)

target_link_libraries(http_server
    PUBLIC Boost::system
    PRIVATE spdlog::spdlog
)
```

#### 4. Example Usage
```cpp
// Option 1: Thread pool server
HttpServer thread_pool_server;
thread_pool_server.set_handler(my_handler);
thread_pool_server.listen(8080);
thread_pool_server.serve_forever();

// Option 2: Asio server (event-driven)
asio::io_context io_context;
HttpServerAsio asio_server(io_context, 8080);
asio_server.set_handler(my_handler);
io_context.run();
```

### Asio Implementation Checklist

- [ ] Install Boost via vcpkg or package manager
- [ ] Update CMakeLists.txt to link Boost::system
- [ ] Create `HttpConnection` class for managing per-connection async I/O
- [ ] Create `HttpServerAsio` class with async acceptor
- [ ] Implement `do_accept()` with async_accept
- [ ] Implement `do_read()` with async_read_some
- [ ] Implement `do_write()` with async_write
- [ ] Add timeout support using `boost::asio::steady_timer`
- [ ] Test with 1000+ concurrent connections
- [ ] Benchmark against thread pool version
- [ ] Document io_context threading model

### Advanced Asio Features

#### 1. Timeouts with Timers
```cpp
class HttpConnection {
    void start() {
        // Set 30-second timeout
        timeout_timer_.expires_after(std::chrono::seconds(30));
        timeout_timer_.async_wait([this](error_code ec) {
            if (!ec) {
                // Timeout expired - close connection
                socket_.close();
            }
        });

        do_read();
    }

    void do_read() {
        async_read_some(..., [this](...) {
            // Cancel timeout on successful read
            timeout_timer_.cancel();

            // Reset timeout for next read
            timeout_timer_.expires_after(std::chrono::seconds(30));
            timeout_timer_.async_wait(...);
        });
    }

    boost::asio::steady_timer timeout_timer_;
};
```

#### 2. Strand for Thread Safety
If using multi-threaded `io_context`, protect shared state with `strand`:

```cpp
class HttpConnection {
    HttpConnection(tcp::socket socket)
        : socket_(std::move(socket))
        , strand_(socket_.get_executor()) {}

    void do_read() {
        async_read_some(
            buffer_,
            // Strand ensures callbacks don't run concurrently
            boost::asio::bind_executor(strand_,
                [this](error_code ec, size_t bytes) {
                    // Thread-safe - only one callback at a time
                }
            )
        );
    }

    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
};
```

#### 3. Coroutines (C++20)
With C++20 coroutines, async code looks synchronous:

```cpp
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/awaitable.hpp>

asio::awaitable<void> handle_connection(tcp::socket socket) {
    try {
        HttpParser parser;
        std::array<char, 8192> buffer;

        while (!parser.is_complete()) {
            // Looks like blocking read, but it's async!
            size_t n = co_await socket.async_read_some(
                asio::buffer(buffer),
                asio::use_awaitable
            );
            parser.parse(buffer.data(), n);
        }

        auto response = handler_(parser.get_request());

        // Async write - looks synchronous!
        co_await asio::async_write(
            socket,
            asio::buffer(response.serialize()),
            asio::use_awaitable
        );

    } catch (std::exception& e) {
        // Handle errors
    }
}

// In acceptor:
co_await acceptor.async_accept(asio::use_awaitable);
co_spawn(io_context, handle_connection(std::move(socket)), detached);
```

**Benefits:**
- No callback hell
- Code reads linearly
- Exception handling with try/catch

---

## Comparison & When to Use Each

### Architecture Summary

| Aspect | Single-Threaded | Thread Pool | Asio Event-Driven |
|--------|----------------|-------------|-------------------|
| **Concurrency** | 1 request at a time | N requests (N = thread count) | Thousands of requests |
| **Thread count** | 1 | 8-32 (configurable) | 1-8 (I/O threads) |
| **Memory/connection** | ~8MB (stack) | ~8MB (stack) | ~few KB (socket state) |
| **Slowloris vulnerable?** | ✅ Very | ✅ Yes | ❌ No |
| **Max connections** | 1 | ~100-1000 | 10,000+ |
| **Complexity** | Low | Medium | High |
| **Platform** | All | All | All (Asio abstracts) |

### Performance Comparison

**Test: 1000 concurrent clients, 100ms handler latency**

| Implementation | Requests/sec | Memory | CPU | Slowloris Resistant? |
|---------------|-------------|--------|-----|---------------------|
| Single-threaded | ~10 | ~8MB | Low | ❌ No |
| Thread pool (N=8) | ~80 | ~64MB | Medium | ❌ No |
| Thread pool (N=32) | ~320 | ~256MB | High | ❌ No |
| Asio (1 I/O thread) | ~1000 | ~20MB | Low | ✅ Yes |
| Asio (8 I/O threads) | ~8000 | ~50MB | Medium | ✅ Yes |

### When to Use Each

#### Use Single-Threaded (Current) When:
- ✅ Learning fundamentals
- ✅ Prototype/proof-of-concept
- ✅ < 10 concurrent users
- ✅ Internal tool, controlled environment

#### Use Thread Pool (Phase 1) When:
- ✅ Learning concurrency (mutexes, CVs, atomics)
- ✅ 10-500 concurrent users
- ✅ Want simple mental model
- ✅ Handlers are CPU-heavy (thread pool keeps cores busy)
- ❌ Don't need to handle malicious slow clients

#### Use Asio Event-Driven (Phase 2) When:
- ✅ 500+ concurrent users
- ✅ Need slowloris protection
- ✅ I/O-bound workload (network, disk)
- ✅ Production deployment
- ✅ Want industry-standard solution
- ❌ Willing to learn async programming model

#### Use Asio + Worker Thread Pool (Phase 2+) When:
- ✅ 1000+ concurrent users
- ✅ Handlers are CPU-heavy
- ✅ Need both concurrency AND parallelism
- ✅ High-performance production server

### Migration Path

```
Phase 0: Single-threaded (CURRENT)
   ↓ Add concurrency
Phase 1: Thread pool
   ↓ Add event-driven I/O
Phase 2: Asio event-driven
   ↓ Optimize for CPU-heavy handlers
Phase 3: Asio + worker thread pool
```

**Recommendation:** Implement Phase 1 (thread pool) first to learn concurrency, then Phase 2 (Asio) to learn async I/O.

---

## References & Further Reading

### Books
- **C++ Concurrency in Action** (Anthony Williams) - Chapter 4 (Thread Pools), Chapter 8 (Async)
- **Effective Modern C++** (Scott Meyers) - Item 40 (std::atomic)
- **Asio C++ Network Programming** (Wisnu Anggoro) - Comprehensive Asio guide

### Documentation
- [Boost.Asio Documentation](https://www.boost.org/doc/libs/release/doc/html/boost_asio.html)
- [Boost.Asio Tutorial](https://www.boost.org/doc/libs/release/doc/html/boost_asio/tutorial.html)
- [IOCP on Windows](https://learn.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports)

### Articles
- [The C10K Problem](http://www.kegel.com/c10k.html) - Classic article on scaling servers
- [Why Events Are A Bad Idea](https://people.eecs.berkeley.edu/~brewer/papers/threads-hotos-2003.pdf) - Threads vs Events debate
- [Asynchronous I/O Patterns](https://think-async.com/Asio/asio-1.28.0/doc/asio/overview/core/async.html)

### Example Projects
- **Beast** (Boost HTTP library built on Asio) - https://github.com/boostorg/beast
- **Crow** (C++ micro web framework) - https://github.com/CrowCpp/Crow
- **cpp-httplib** (Single-header HTTP library with thread pool) - https://github.com/yhirose/cpp-httplib

---

## Code Reuse Guide: What Changes Between Implementations

This section explains **exactly** which parts of your existing code you'll reuse and which parts need to change for each implementation approach.

### Current Codebase Structure

**Your existing files:**
```
include/dfs/network/
├── socket.hpp           - Cross-platform socket wrapper (POSIX/Winsock)
├── http_parser.hpp      - Incremental HTTP/1.1 parser
├── http_types.hpp       - HttpRequest, HttpResponse, HttpStatus, etc.
└── http_server.hpp      - Single-threaded blocking server

src/network/
├── socket.cpp           - Socket implementation
├── http_parser.cpp      - Parser implementation
├── http_types.cpp       - HTTP types implementation
└── http_server.cpp      - Server implementation (lines 1-249)
```

### Phase 1: Thread Pool - What Changes?

**New file structure:**
```
include/dfs/network/
├── socket.hpp           ← NO CHANGES (keep as-is)
├── http_parser.hpp      ← NO CHANGES (keep as-is)
├── http_types.hpp       ← NO CHANGES (keep as-is)
└── http_server.hpp      ← MODIFY THIS

src/network/
├── socket.cpp           ← NO CHANGES (keep as-is)
├── http_parser.cpp      ← NO CHANGES (keep as-is)
├── http_types.cpp       ← NO CHANGES (keep as-is)
└── http_server.cpp      ← MODIFY THIS
```

#### Changes to `http_server.hpp`

**What stays the same:**
```cpp
// Lines 15-36: HttpRequestHandler type - NO CHANGE
using HttpRequestHandler = std::function<HttpResponse(const HttpRequest&)>;

// Lines 74-144: Public API - NO CHANGE
class HttpServer {
public:
    void set_handler(HttpRequestHandler handler);
    Result<void> listen(uint16_t port, const std::string& address = "0.0.0.0");
    Result<void> serve_forever();
    void stop();
    bool is_running() const;
    uint16_t get_port() const;
};
```

**What changes - Add to private section (after line 149):**
```cpp
private:
    // EXISTING members (lines 146-149) - KEEP THESE
    Socket listener_;
    HttpRequestHandler handler_;
    bool running_;                    // ← CHANGE to std::atomic<bool>
    uint16_t port_;

    // NEW members - ADD THESE
    // Thread pool
    std::vector<std::thread> worker_threads_;
    size_t thread_pool_size_;

    // Task queue
    std::queue<std::unique_ptr<Socket>> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    size_t max_queue_size_;

    // Monitoring
    std::atomic<size_t> active_connections_;
    std::atomic<size_t> total_processed_;

    // NEW private method - ADD THIS
    void worker_thread();

    // EXISTING methods (lines 151-199) - KEEP THESE
    Result<void> handle_connection(std::unique_ptr<Socket> client);
    Result<HttpRequest> read_request(Socket& socket, HttpParser& parser);
    Result<void> send_response(Socket& socket, const HttpResponse& response);
    HttpResponse create_error_response(HttpStatus status, const std::string& message);
};
```

**Summary of header changes:**
- ✅ Keep: All public API, all existing helper methods
- ✅ Keep: Socket, HttpParser, HttpTypes usage
- ➕ Add: Thread pool members (vector, queue, mutex, cv)
- ➕ Add: Atomic counters for monitoring
- 🔧 Modify: `bool running_` → `std::atomic<bool> running_`
- ➕ Add: New `worker_thread()` private method

#### Changes to `http_server.cpp`

**Line-by-line modification guide:**

**Lines 1-23: Constructor/Destructor/set_handler - MINIMAL CHANGE**
```cpp
// OLD (lines 11-14)
HttpServer::HttpServer()
    : running_(false)
    , port_(0) {
}

// NEW - Add thread pool initialization
HttpServer::HttpServer()
    : running_(false)
    , port_(0)
    , thread_pool_size_(std::thread::hardware_concurrency() * 2)
    , max_queue_size_(1000)
    , active_connections_(0)
    , total_processed_(0) {
}
```

**Lines 24-55: listen() - NO CHANGES**
```cpp
// Lines 24-55: Stays EXACTLY the same
Result<void> HttpServer::listen(uint16_t port, const std::string& address) {
    // ... no changes needed
}
```

**Lines 57-96: serve_forever() - MAJOR CHANGES**
```cpp
// OLD (lines 57-96) - Single-threaded blocking loop
Result<void> HttpServer::serve_forever() {
    running_ = true;

    while (running_) {
        auto accept_result = listener_.accept();
        auto client = std::move(accept_result.value());
        handle_connection(std::move(client));  // ← Blocks here
    }
}

// NEW - Spawn workers, then enqueue connections
Result<void> HttpServer::serve_forever() {
    if (!listener_.is_valid()) {
        return Err<void, std::string>("Server not initialized. Call listen() first.");
    }
    if (!handler_) {
        return Err<void, std::string>("No request handler set. Call set_handler() first.");
    }

    // Spawn worker threads
    for (size_t i = 0; i < thread_pool_size_; ++i) {
        worker_threads_.emplace_back([this] { worker_thread(); });
    }

    running_.store(true, std::memory_order_release);
    spdlog::info("Server started with {} worker threads", thread_pool_size_);

    // Accept loop - just enqueue connections
    while (running_.load(std::memory_order_acquire)) {
        auto accept_result = listener_.accept();
        if (accept_result.is_error()) {
            if (!running_.load(std::memory_order_acquire)) break;
            spdlog::error("Failed to accept connection: {}", accept_result.error());
            continue;
        }

        auto client = std::move(accept_result.value());

        // Enqueue with overflow protection
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (task_queue_.size() >= max_queue_size_) {
                spdlog::warn("Queue full ({} tasks), rejecting connection", max_queue_size_);
                // Send 503 response
                auto overload = create_error_response(
                    HttpStatus::SERVICE_UNAVAILABLE,
                    "Server overloaded, try again later"
                );
                send_response(*client, overload);
                client->close();
                continue;
            }
            task_queue_.push(std::move(client));
        }

        // Wake one worker
        queue_cv_.notify_one();
    }

    spdlog::info("Server stopped");
    return Ok();
}

// ADD NEW METHOD - Worker thread function
void HttpServer::worker_thread() {
    while (true) {
        std::unique_ptr<Socket> client;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            // Wait for task or shutdown signal
            queue_cv_.wait(lock, [this] {
                return !task_queue_.empty() || !running_.load(std::memory_order_acquire);
            });

            // Check shutdown
            if (!running_.load(std::memory_order_acquire) && task_queue_.empty()) {
                break;  // Exit thread
            }

            // Get task
            if (!task_queue_.empty()) {
                client = std::move(task_queue_.front());
                task_queue_.pop();
            }
        }  // Release lock here!

        // Process connection WITHOUT holding lock
        if (client) {
            active_connections_.fetch_add(1, std::memory_order_relaxed);
            handle_connection(std::move(client));
            active_connections_.fetch_sub(1, std::memory_order_relaxed);
            total_processed_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    spdlog::debug("Worker thread exiting");
}
```

**Lines 98-104: stop() - MAJOR CHANGES**
```cpp
// OLD (lines 98-104) - Just close socket
void HttpServer::stop() {
    if (running_) {
        running_ = false;
        listener_.close();
    }
}

// NEW - Graceful shutdown with thread joining
void HttpServer::stop() {
    if (running_.load(std::memory_order_acquire)) {
        spdlog::info("Stopping server...");

        // Signal shutdown
        running_.store(false, std::memory_order_release);

        // Wake all workers
        queue_cv_.notify_all();

        // Close listener to unblock accept()
        listener_.close();

        // Join all workers
        for (auto& thread : worker_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        worker_threads_.clear();

        spdlog::info("Server stopped. Processed {} total requests",
                     total_processed_.load());
    }
}
```

**Lines 106-160: handle_connection() - NO CHANGES**
```cpp
// Lines 106-160: Stays EXACTLY the same
Result<void> HttpServer::handle_connection(std::unique_ptr<Socket> client) {
    // ... no changes needed
}
```

**Lines 162-199: read_request() - NO CHANGES**
```cpp
// Lines 162-199: Stays EXACTLY the same
Result<HttpRequest> HttpServer::read_request(Socket& socket, HttpParser& parser) {
    // ... no changes needed
}
```

**Lines 201-225: send_response() - NO CHANGES**
```cpp
// Lines 201-225: Stays EXACTLY the same
Result<void> HttpServer::send_response(Socket& socket, const HttpResponse& response) {
    // ... no changes needed
}
```

**Lines 227-246: create_error_response() - NO CHANGES**
```cpp
// Lines 227-246: Stays EXACTLY the same
HttpResponse HttpServer::create_error_response(HttpStatus status, const std::string& message) {
    // ... no changes needed
}
```

#### Phase 1 Summary: Code Reuse Statistics

| Component | Lines of Code | Changes |
|-----------|---------------|---------|
| **Socket class** | ~500 lines | ✅ 0% - No changes |
| **HttpParser** | ~300 lines | ✅ 0% - No changes |
| **HttpTypes** | ~400 lines | ✅ 0% - No changes |
| **HttpServer header** | ~200 lines | 🔧 10% - Add members |
| **HttpServer impl** | ~250 lines | 🔧 30% - Modify 3 methods, add 1 new |
| **handle_connection()** | ~55 lines | ✅ 0% - No changes |
| **read_request()** | ~38 lines | ✅ 0% - No changes |
| **send_response()** | ~25 lines | ✅ 0% - No changes |
| **create_error_response()** | ~20 lines | ✅ 0% - No changes |

**Total reuse: ~85% of your code unchanged!**

---

### Phase 2: Boost.Asio - What Changes?

**New file structure (keep Phase 1 version!):**
```
include/dfs/network/
├── socket.hpp           ← NO CHANGES (keep for thread pool version)
├── http_parser.hpp      ← NO CHANGES (shared by both)
├── http_types.hpp       ← NO CHANGES (shared by both)
├── http_server.hpp      ← NO CHANGES (thread pool version)
└── http_server_asio.hpp ← NEW FILE (Asio version)

src/network/
├── socket.cpp           ← NO CHANGES
├── http_parser.cpp      ← NO CHANGES
├── http_types.cpp       ← NO CHANGES
├── http_server.cpp      ← NO CHANGES (thread pool version)
└── http_server_asio.cpp ← NEW FILE (Asio version)
```

#### What Gets Reused from Your Code

**100% Reused - Zero Changes:**
```
✅ http_parser.hpp/cpp       - All parsing logic
✅ http_types.hpp/cpp        - All HTTP types, serialization
✅ HttpRequestHandler type   - Same function signature
```

**NOT Reused - Replaced by Asio:**
```
❌ socket.hpp/cpp           - Replaced by boost::asio::ip::tcp::socket
❌ HttpServer class         - New HttpServerAsio class
```

**Partially Reused - Logic copied, paradigm changed:**
```
🔄 handle_connection()      - Logic reused, but async callbacks instead of blocking
🔄 create_error_response()  - Exact same function, called from async context
```

#### New File: `http_server_asio.hpp`

**What it looks like:**
```cpp
#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include "http_parser.hpp"    // ← REUSED from your code
#include "http_types.hpp"     // ← REUSED from your code
#include "dfs/core/result.hpp"
#include <memory>

namespace dfs {
namespace network {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

// ← REUSED: Same handler type as your HttpServer!
using HttpRequestHandler = std::function<HttpResponse(const HttpRequest&)>;

// NEW: Per-connection handler (replaces blocking handle_connection)
class HttpConnection : public std::enable_shared_from_this<HttpConnection> {
public:
    HttpConnection(tcp::socket socket, HttpRequestHandler handler);
    void start();

private:
    void do_read();
    void do_write(const HttpResponse& response);
    void handle_error(const std::string& message);

    tcp::socket socket_;          // NEW: Asio socket (replaces your Socket)
    HttpRequestHandler handler_;  // REUSED: Same handler type
    HttpParser parser_;           // REUSED: Your parser!
    std::array<char, 8192> buffer_;
};

// NEW: Asio-based server class
class HttpServerAsio {
public:
    HttpServerAsio(asio::io_context& io_context, uint16_t port);

    // REUSED: Same API as your HttpServer
    void set_handler(HttpRequestHandler handler);
    uint16_t get_port() const { return port_; }

private:
    void do_accept();

    // NEW: Creates error responses (same logic as your create_error_response)
    HttpResponse create_error_response(HttpStatus status, const std::string& message);

    tcp::acceptor acceptor_;
    asio::io_context& io_context_;
    HttpRequestHandler handler_;  // REUSED: Same handler type
    uint16_t port_;
};

} // namespace network
} // namespace dfs
```

#### New File: `http_server_asio.cpp`

**What gets reused from your http_server.cpp:**

```cpp
#include "dfs/network/http_server_asio.hpp"
#include <spdlog/spdlog.h>

namespace dfs {
namespace network {

// ──────────────────────────────────────────────────────────
// HttpConnection Implementation
// ──────────────────────────────────────────────────────────

HttpConnection::HttpConnection(tcp::socket socket, HttpRequestHandler handler)
    : socket_(std::move(socket))
    , handler_(std::move(handler))  // ← REUSED: Same handler pattern
    , parser_() {                   // ← REUSED: Your HttpParser
}

void HttpConnection::start() {
    do_read();
}

void HttpConnection::do_read() {
    auto self = shared_from_this();

    // NEW: Async read (replaces blocking socket.receive())
    socket_.async_read_some(
        asio::buffer(buffer_),
        [this, self](boost::system::error_code ec, size_t bytes) {
            if (!ec) {
                // ────────────────────────────────────────
                // REUSED: Exact same parsing logic from your read_request()
                // (http_server.cpp lines 180-188)
                // ────────────────────────────────────────
                auto parse_result = parser_.parse(
                    buffer_.data(),
                    bytes
                );

                if (parse_result.is_error()) {
                    handle_error("Parse error: " + parse_result.error());
                    return;
                }

                if (parse_result.value()) {
                    // ────────────────────────────────────────
                    // REUSED: Same request handling logic
                    // (http_server.cpp lines 122-146)
                    // ────────────────────────────────────────
                    HttpRequest request = parser_.get_request();

                    spdlog::info("{} {} HTTP/{}",
                        HttpMethodUtils::to_string(request.method),
                        request.url,
                        request.version == HttpVersion::HTTP_1_1 ? "1.1" : "1.0");

                    HttpResponse response;
                    try {
                        response = handler_(request);  // ← REUSED: Same handler call
                    } catch (const std::exception& e) {
                        spdlog::error("Handler threw exception: {}", e.what());
                        response = create_error_response(
                            HttpStatus::INTERNAL_SERVER_ERROR,
                            "Internal server error"
                        );
                    }

                    do_write(response);
                } else {
                    // Need more data - continue reading
                    do_read();
                }
            } else {
                spdlog::error("Read error: {}", ec.message());
            }
        }
    );
}

void HttpConnection::do_write(const HttpResponse& response) {
    auto self = shared_from_this();

    // ────────────────────────────────────────
    // REUSED: Exact same serialization from your send_response()
    // (http_server.cpp line 203)
    // ────────────────────────────────────────
    std::vector<uint8_t> data = response.serialize();

    // NEW: Async write (replaces blocking socket.send())
    asio::async_write(
        socket_,
        asio::buffer(data),
        [this, self](boost::system::error_code ec, size_t bytes) {
            if (!ec) {
                spdlog::debug("Sent {} bytes", bytes);
                socket_.shutdown(tcp::socket::shutdown_both, ec);
            } else {
                spdlog::error("Write error: {}", ec.message());
            }
        }
    );
}

void HttpConnection::handle_error(const std::string& message) {
    // ────────────────────────────────────────
    // REUSED: Same error handling logic from your handle_connection()
    // (http_server.cpp lines 114-119)
    // ────────────────────────────────────────
    auto error_response = create_error_response(
        HttpStatus::BAD_REQUEST,
        message
    );
    do_write(error_response);
}

// ──────────────────────────────────────────────────────────
// HttpServerAsio Implementation
// ──────────────────────────────────────────────────────────

HttpServerAsio::HttpServerAsio(asio::io_context& io_context, uint16_t port)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
    , io_context_(io_context)
    , port_(port) {

    spdlog::info("HTTP server (Asio) listening on port {}", port);
    do_accept();
}

void HttpServerAsio::set_handler(HttpRequestHandler handler) {
    // ────────────────────────────────────────
    // REUSED: Exact same from http_server.cpp line 20-22
    // ────────────────────────────────────────
    handler_ = std::move(handler);
}

void HttpServerAsio::do_accept() {
    // NEW: Async accept (replaces blocking listener_.accept())
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                spdlog::debug("Accepted new connection");

                // Create connection handler (replaces direct handle_connection call)
                std::make_shared<HttpConnection>(
                    std::move(socket),
                    handler_
                )->start();
            } else {
                spdlog::error("Accept error: {}", ec.message());
            }

            // Accept next connection (recursive)
            do_accept();
        }
    );
}

// ────────────────────────────────────────
// REUSED: EXACT COPY from http_server.cpp lines 227-246
// ────────────────────────────────────────
HttpResponse HttpServerAsio::create_error_response(HttpStatus status, const std::string& message) {
    HttpResponse response(status);

    std::string html =
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head><title>Error " + std::to_string(static_cast<int>(status)) + "</title></head>\n"
        "<body>\n"
        "<h1>Error " + std::to_string(static_cast<int>(status)) + "</h1>\n"
        "<p>" + message + "</p>\n"
        "</body>\n"
        "</html>\n";

    response.set_body(html);
    response.set_header("Content-Type", "text/html");
    response.set_header("Connection", "close");

    return response;
}

} // namespace network
} // namespace dfs
```

#### Phase 2 Summary: Code Reuse from YOUR Code

| Your Component | Used in Asio Version? | How? |
|----------------|----------------------|------|
| **http_parser.hpp/cpp** | ✅ YES - 100% | Included directly, no changes |
| **http_types.hpp/cpp** | ✅ YES - 100% | Included directly, no changes |
| **HttpRequestHandler** | ✅ YES - 100% | Same type signature |
| **socket.hpp/cpp** | ❌ NO | Replaced by `boost::asio::ip::tcp::socket` |
| **parse logic** (lines 180-188) | ✅ YES - 100% | Copied to `do_read()` callback |
| **handler call** (lines 130-146) | ✅ YES - 100% | Copied to `do_read()` callback |
| **response serialize** (line 203) | ✅ YES - 100% | Copied to `do_write()` |
| **create_error_response()** | ✅ YES - 100% | Exact copy (lines 227-246) |
| **set_handler()** | ✅ YES - 100% | Exact copy (lines 20-22) |

**Code reuse: ~60% of your HTTP logic reused, only socket I/O layer replaced**

---

## Side-by-Side Comparison: What Changes Between Versions

### Your Code (Current Single-Threaded)

```cpp
// http_server.cpp
Result<void> HttpServer::serve_forever() {
    running_ = true;

    while (running_) {
        // BLOCKS waiting for connection
        auto accept_result = listener_.accept();
        auto client = std::move(accept_result.value());

        // BLOCKS processing request
        handle_connection(std::move(client));
    }
}

Result<void> handle_connection(std::unique_ptr<Socket> client) {
    HttpParser parser;

    // BLOCKS reading request
    auto request_result = read_request(*client, parser);
    HttpRequest request = request_result.value();

    // Call handler (your code - unchanged)
    HttpResponse response = handler_(request);

    // BLOCKS sending response
    send_response(*client, response);
}
```

### Phase 1: Thread Pool (Modified Your Code)

```cpp
// http_server.cpp - MODIFIED
Result<void> HttpServer::serve_forever() {
    // NEW: Spawn workers
    for (size_t i = 0; i < thread_pool_size_; ++i) {
        worker_threads_.emplace_back([this] { worker_thread(); });
    }

    running_ = true;

    while (running_) {
        // STILL BLOCKS, but only here
        auto accept_result = listener_.accept();
        auto client = std::move(accept_result.value());

        // NEW: Enqueue instead of processing
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            task_queue_.push(std::move(client));
        }
        queue_cv_.notify_one();  // Wake worker
    }
}

// NEW FUNCTION
void HttpServer::worker_thread() {
    while (true) {
        std::unique_ptr<Socket> client;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !task_queue_.empty() || !running_;
            });

            if (!running_ && task_queue_.empty()) break;

            client = std::move(task_queue_.front());
            task_queue_.pop();
        }

        if (client) {
            // REUSED: Same handle_connection from your code!
            handle_connection(std::move(client));
        }
    }
}

// NO CHANGES - still uses your Socket class
Result<void> handle_connection(std::unique_ptr<Socket> client) {
    // ... exact same as your original code
}
```

### Phase 2: Boost.Asio (New File, Reuses HTTP Logic)

```cpp
// http_server_asio.cpp - NEW FILE
HttpServerAsio::HttpServerAsio(asio::io_context& io_context, uint16_t port)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
    do_accept();  // Start async accept loop
}

void HttpServerAsio::do_accept() {
    // NEVER BLOCKS
    acceptor_.async_accept([this](error_code ec, tcp::socket socket) {
        if (!ec) {
            // Create connection handler
            std::make_shared<HttpConnection>(
                std::move(socket),
                handler_  // ← REUSED: Your handler type
            )->start();
        }

        do_accept();  // Accept next (recursive)
    });
}

class HttpConnection {
    void do_read() {
        // NEVER BLOCKS
        socket_.async_read_some(buffer_, [this](error_code ec, size_t bytes) {
            // ─────────────────────────────────────
            // REUSED: Your parsing logic (http_server.cpp:180-188)
            // ─────────────────────────────────────
            auto parse_result = parser_.parse(buffer_.data(), bytes);

            if (parse_result.value()) {
                HttpRequest request = parser_.get_request();

                // REUSED: Your handler call (http_server.cpp:133)
                HttpResponse response = handler_(request);

                do_write(response);
            } else {
                do_read();  // Need more data
            }
        });
    }

    void do_write(const HttpResponse& response) {
        // ─────────────────────────────────────
        // REUSED: Your serialization (http_server.cpp:203)
        // ─────────────────────────────────────
        auto data = response.serialize();

        // NEVER BLOCKS
        asio::async_write(socket_, asio::buffer(data),
            [this](error_code ec, size_t bytes) {
                socket_.close();
            }
        );
    }

    HttpParser parser_;     // ← REUSED: Your parser
    HttpRequestHandler handler_;  // ← REUSED: Your handler type
};
```

---

## Quick Reference: What to Import/Include

### Phase 1: Thread Pool

**Header includes to add:**
```cpp
#include <thread>               // std::thread
#include <queue>                // std::queue
#include <mutex>                // std::mutex, std::lock_guard, std::unique_lock
#include <condition_variable>   // std::condition_variable
#include <atomic>               // std::atomic
```

**Your existing includes - keep them all:**
```cpp
#include "dfs/network/socket.hpp"      // ✅ KEEP
#include "dfs/network/http_parser.hpp" // ✅ KEEP
#include "dfs/network/http_types.hpp"  // ✅ KEEP
#include "dfs/core/result.hpp"         // ✅ KEEP
```

### Phase 2: Boost.Asio

**Header includes:**
```cpp
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include "dfs/network/http_parser.hpp"  // ✅ REUSE yours
#include "dfs/network/http_types.hpp"   // ✅ REUSE yours
#include "dfs/core/result.hpp"          // ✅ REUSE yours
#include <memory>                       // std::shared_ptr, std::enable_shared_from_this
```

**What you DON'T include:**
```cpp
// ❌ DON'T include - Asio replaces this
// #include "dfs/network/socket.hpp"
```

---

## Decision Matrix: Which Code to Use When

### Scenario: Learning Concurrency Basics
**Use:** Phase 1 (Thread Pool)
**Your code reused:**
- ✅ 100% of Socket class
- ✅ 100% of HTTP parsing
- ✅ 85% of HttpServer implementation
**New concepts:** `std::thread`, `std::mutex`, `std::condition_variable`, `std::atomic`

### Scenario: Building Production Server (< 1000 users)
**Use:** Phase 1 (Thread Pool)
**Why:** Simpler, uses all your existing code, sufficient performance

### Scenario: Building Production Server (> 1000 users)
**Use:** Phase 2 (Boost.Asio)
**Your code reused:**
- ✅ 100% of HTTP parsing
- ✅ 100% of HTTP types
- ✅ 60% of HTTP handling logic
**New dependency:** Boost.Asio library

### Scenario: Need Slowloris Protection
**Use:** Phase 2 (Boost.Asio)
**Why:** Event-driven I/O doesn't tie up threads waiting for slow clients

### Scenario: Want to Learn Both Approaches
**Use:** Implement both in parallel
**File structure:**
```
├── http_server.hpp/cpp       ← Phase 1 (your Socket class)
├── http_server_asio.hpp/cpp  ← Phase 2 (Asio sockets)
├── http_parser.hpp/cpp       ← Shared by both ✅
├── http_types.hpp/cpp        ← Shared by both ✅
└── socket.hpp/cpp            ← Used by Phase 1 only
```

---

## Summary: Your Code's Journey

### What You Built (Phase 0)
```
Socket class (500 lines)
   ↓
HttpParser (300 lines)
   ↓
HttpTypes (400 lines)
   ↓
HttpServer (250 lines)
```
**Total: ~1450 lines of your code**

### Phase 1 Transformation
```
Socket class (500 lines)          ✅ UNCHANGED
   ↓
HttpParser (300 lines)            ✅ UNCHANGED
   ↓
HttpTypes (400 lines)             ✅ UNCHANGED
   ↓
HttpServer (250 + 50 lines)       🔧 MODIFIED (add thread pool)
```
**Reuse: ~93% of your code**
**New: 50 lines (thread pool logic)**

### Phase 2 Transformation
```
Socket class (500 lines)          ❌ NOT USED (Asio replaces)
   ↓
HttpParser (300 lines)            ✅ REUSED 100%
   ↓
HttpTypes (400 lines)             ✅ REUSED 100%
   ↓
HttpServerAsio (300 lines new)    ➕ NEW FILE (reuses 60% of logic)
```
**Reuse from your code: ~60% (all HTTP parsing/types logic)**
**New: Event-driven I/O pattern with Asio sockets**

**Key insight:** The HTTP protocol handling you built (parser, types, request/response) is valuable and reusable. Only the I/O layer (sockets) changes between implementations!
