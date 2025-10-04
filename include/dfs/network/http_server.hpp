#pragma once

#include "socket.hpp"
#include "http_parser.hpp"
#include "http_types.hpp"
#include "dfs/core/result.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace dfs {
namespace network {

/**
 * @brief HTTP request handler function type
 *
 * This is the signature for request handler functions. Users of HttpServer
 * provide a function with this signature to handle incoming requests.
 */
using HttpRequestHandler = std::function<HttpResponse(const HttpRequest&)>;

/**
 * @brief Multi-threaded HTTP/1.1 server with thread pool
 *
 * This server uses a thread pool to handle multiple concurrent connections.
 * It provides better performance than the single-threaded version while
 * maintaining bounded resource usage.
 *
 * Architecture:
 * - Main acceptor thread: Accepts connections and enqueues them
 * - Worker thread pool: Fixed number of threads that process requests
 * - Task queue: Decouples connection acceptance from processing
 * - Condition variables: Efficient synchronization without busy-waiting
 *
 * Key features:
 * - Concurrent request handling (configurable thread count)
 * - HTTP/1.1 compliant parsing
 * - Graceful shutdown with thread joining
 * - Queue overflow protection (503 responses)
 * - Connection tracking and monitoring
 *
 * Thread safety:
 * - All public methods are thread-safe
 * - Handler function may be called concurrently from multiple threads
 *
 * Usage:
 * ```cpp
 * HttpServer server(8);  // 8 worker threads
 * server.set_handler([](const HttpRequest& req) {
 *     HttpResponse res(HttpStatus::OK);
 *     res.set_body("Hello from thread pool!");
 *     return res;
 * });
 * auto result = server.listen(8080);
 * if (result.is_ok()) {
 *     server.serve_forever();
 * }
 * ```
 */
class HttpServer {
public:
    /**
     * @brief Construct HTTP server with specified thread pool size
     *
     * @param thread_pool_size Number of worker threads (default: 2x CPU cores)
     * @param max_queue_size Maximum pending connections (default: 1000)
     */
    explicit HttpServer(
        size_t thread_pool_size = std::thread::hardware_concurrency() * 2,
        size_t max_queue_size = 1000
    );

    ~HttpServer();

    // Prevent copying (server owns threads and sockets)
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Prevent moving (complex resource ownership)
    HttpServer(HttpServer&&) = delete;
    HttpServer& operator=(HttpServer&&) = delete;

    /**
     * @brief Set the request handler function
     *
     * This function will be called for each incoming HTTP request.
     * It may be called concurrently from multiple worker threads.
     *
     * THREAD SAFETY: Ensure your handler is thread-safe if it accesses
     * shared state.
     *
     * @param handler Function to handle requests
     */
    void set_handler(HttpRequestHandler handler);

    /**
     * @brief Start listening on a port
     *
     * Binds to the specified port and starts listening for connections.
     *
     * @param port Port number to listen on
     * @param address Address to bind to (default: "0.0.0.0" - all interfaces)
     * @return Result indicating success or error
     */
    Result<void> listen(uint16_t port, const std::string& address = "0.0.0.0");

    /**
     * @brief Run the server (blocking)
     *
     * Spawns worker threads and enters the main accept loop.
     * Runs until stop() is called or an error occurs.
     *
     * @return Result indicating if server stopped cleanly or with error
     */
    Result<void> serve_forever();

    /**
     * @brief Stop the server gracefully
     *
     * Signals all worker threads to stop, wakes them up, and joins them.
     * The serve_forever() loop will exit after cleanup completes.
     */
    void stop();

    /**
     * @brief Check if server is currently running
     */
    bool is_running() const { return running_.load(std::memory_order_acquire); }

    /**
     * @brief Get the port the server is listening on
     */
    uint16_t get_port() const { return port_; }

    /**
     * @brief Get current number of active connections
     */
    size_t get_active_connections() const {
        return active_connections_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Get total number of requests processed
     */
    size_t get_total_processed() const {
        return total_processed_.load(std::memory_order_relaxed);
    }

private:
    // Socket and handler
    Socket listener_;                 // Listening socket
    HttpRequestHandler handler_;      // User-provided request handler
    uint16_t port_;                   // Port we're listening on

    // Thread pool
    std::vector<std::thread> worker_threads_;
    size_t thread_pool_size_;

    // Task queue
    std::queue<std::unique_ptr<Socket>> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    size_t max_queue_size_;

    // State management (atomic for thread-safe access)
    std::atomic<bool> running_;
    std::atomic<size_t> active_connections_;
    std::atomic<size_t> total_processed_;

    /**
     * @brief Worker thread function
     *
     * Each worker thread runs this function in a loop:
     * 1. Wait for task in queue (blocking on condition variable)
     * 2. Pop task from queue
     * 3. Process the connection
     * 4. Repeat until shutdown signal
     */
    void worker_thread();

    /**
     * @brief Handle a single client connection
     *
     * Called by worker threads to process a connection.
     * This is the same logic as the legacy single-threaded version.
     *
     * @param client Socket for the client connection
     * @return Result indicating success or error
     */
    Result<void> handle_connection(std::unique_ptr<Socket> client);

    /**
     * @brief Read a complete HTTP request from socket
     */
    Result<HttpRequest> read_request(Socket& socket, HttpParser& parser);

    /**
     * @brief Send HTTP response to client
     */
    Result<void> send_response(Socket& socket, const HttpResponse& response);

    /**
     * @brief Create an error response
     */
    HttpResponse create_error_response(HttpStatus status, const std::string& message);
};

} // namespace network
} // namespace dfs
