# Multi-threaded HTTP Server Implementation Plan

## Current Architecture

The current HTTP server (`http_server.cpp`) is single-threaded and blocking:
- `serve_forever()` accepts connections sequentially (line 73)
- `handle_connection()` processes each request synchronously (line 106-159)
- Subsequent connections wait until the current one completes

**Limitation:** Only one client can be served at a time, causing head-of-line blocking.

## Multi-threaded Architecture Design

### Overview

Transform the server to handle multiple concurrent connections using:
1. **Main acceptor thread** - Accepts connections and enqueues them
2. **Thread pool** - Fixed number of worker threads that process requests
3. **Task queue** - Decouples connection acceptance from request processing
4. **Condition variables** - Efficient thread synchronization (no busy-waiting)

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

## What NOT to Over-Engineer (Phase 1)

### ❌ Lock-Free Queues
- **Complexity:** Hard to implement correctly, subtle bugs
- **Gains:** Marginal for HTTP server workload
- **Verdict:** Use `std::queue` + `std::mutex` (proven, debuggable)

### ❌ Work-Stealing Schedulers
- **Complexity:** Each thread has own queue, steals from others when idle
- **Gains:** Better load balancing for heterogeneous tasks
- **Verdict:** Overkill for uniform HTTP request handling

### ❌ io_uring / epoll (for now)
- **Paradigm shift:** Event-driven async I/O (not thread-per-request)
- **Complexity:** Completely different programming model
- **Verdict:** Save for Phase 2+ when you need 10k+ connections

### ❌ std::jthread (C++20)
- **Nice feature:** Auto-joins on destruction, stop tokens built-in
- **Required?** No - manual `join()` in destructor works fine
- **Verdict:** Use if C++20 available, not essential

## Implementation Checklist

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

## Testing Strategy

### 1. Correctness
```bash
# Terminal 1: Start server
./http_server

# Terminal 2: Concurrent requests
for i in {1..100}; do
  curl http://localhost:8080/test &
done
wait
```

### 2. Load Testing
```bash
# Apache Bench
ab -n 10000 -c 100 http://localhost:8080/

# wrk (modern alternative)
wrk -t4 -c100 -d30s http://localhost:8080/
```

### 3. Stress Testing (Queue Limits)
```bash
# Flood server to trigger 503 responses
ab -n 100000 -c 500 http://localhost:8080/
# Should see "Queue full" warnings in logs
```

## Performance Expectations

**Before (Single-threaded):**
- Requests/sec: ~100-500 (depends on handler latency)
- Concurrency: 1
- Latency under load: Linearly increases

**After (Thread Pool with N=8):**
- Requests/sec: ~800-4000 (8x improvement if I/O bound)
- Concurrency: 8 simultaneous requests
- Latency under load: Stable until saturation

**Bottleneck shift:** From connection acceptance to queue/thread availability.

## Future Enhancements (Phase 2+)

1. **Async I/O (epoll/io_uring):** Handle 10k+ connections with few threads
2. **Connection Keep-Alive:** Reuse TCP connections for multiple requests
3. **HTTP/2 Support:** Multiplexing, header compression
4. **SSL/TLS:** Encrypted connections
5. **Request Prioritization:** QoS-aware queue (priority queue)
6. **Dynamic Thread Pool:** Scale workers based on load
7. **Metrics/Observability:** Prometheus exports, distributed tracing

## References

- C++ Concurrency in Action (Anthony Williams) - Chapter 4 (Thread Pools)
- Effective Modern C++ (Scott Meyers) - Item 40 (std::atomic)
- Linux System Programming (Robert Love) - Chapter 8 (Thread Synchronization)
