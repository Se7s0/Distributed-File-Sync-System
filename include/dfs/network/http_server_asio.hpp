#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include "http_parser.hpp"
#include "http_types.hpp"
#include "dfs/core/result.hpp"
#include <memory>
#include <array>

namespace dfs {
namespace network {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

// Reuse the same handler type
using HttpRequestHandler = std::function<HttpResponse(const HttpRequest&)>;

/**
 * @brief Per-connection handler for async HTTP requests
 *
 * Each accepted connection gets its own HttpConnection object that manages
 * the async I/O for that connection. Uses enable_shared_from_this to keep
 * the connection alive while async operations are pending.
 *
 * Lifecycle:
 * 1. Created when connection is accepted
 * 2. start() begins async read operation
 * 3. Callbacks handle data arrival and parsing
 * 4. Destroyed when connection closes or error occurs
 */
class HttpConnection : public std::enable_shared_from_this<HttpConnection> {
public:
    HttpConnection(tcp::socket socket, HttpRequestHandler handler);

    /**
     * @brief Start async reading from the connection
     *
     * Initiates the async I/O chain. Does not block.
     */
    void start();

private:
    /**
     * @brief Async read handler
     *
     * Called by Boost.Asio when data arrives on the socket.
     * Parses the data and either continues reading or processes the request.
     */
    void do_read();

    /**
     * @brief Async write handler
     *
     * Sends the HTTP response asynchronously.
     *
     * @param response Response to send
     */
    void do_write(const HttpResponse& response);

    /**
     * @brief Handle parsing/protocol errors
     *
     * @param message Error message
     */
    void handle_error(const std::string& message);

    /**
     * @brief Create error response (same logic as other servers)
     */
    HttpResponse create_error_response(HttpStatus status, const std::string& message);

    tcp::socket socket_;                    // Asio socket (non-blocking)
    HttpRequestHandler handler_;            // User-provided handler
    HttpParser parser_;                     // HTTP parser (reused from existing code!)
    std::array<char, 8192> buffer_;         // Read buffer
};

/**
 * @brief Event-driven HTTP server using Boost.Asio
 *
 * This server uses asynchronous I/O to handle thousands of concurrent
 * connections with minimal thread overhead. It solves the slowloris
 * vulnerability that affects thread-based servers.
 *
 * Architecture:
 * - Async accept: Non-blocking connection acceptance
 * - Event loop (io_context): OS-level notification system (IOCP on Windows)
 * - Per-connection objects: Each connection has its own async I/O state
 *
 * Advantages over thread pool:
 * - Handles 10,000+ concurrent connections
 * - Slow clients don't consume threads (just memory)
 * - Resistant to slowloris DoS attacks
 * - Lower memory footprint (~few KB per connection vs ~8MB per thread)
 *
 * Thread safety:
 * - Single-threaded by default (run io_context.run() in one thread)
 * - Can run multi-threaded (call io_context.run() from multiple threads)
 * - Handler may be called from io_context thread(s)
 *
 * Usage:
 * ```cpp
 * asio::io_context io_context;
 * HttpServerAsio server(io_context, 8080);
 * server.set_handler([](const HttpRequest& req) {
 *     HttpResponse res(HttpStatus::OK);
 *     res.set_body("Hello from Asio!");
 *     return res;
 * });
 * io_context.run();  // Blocks here, running event loop
 * ```
 */
class HttpServerAsio {
public:
    /**
     * @brief Construct Asio-based HTTP server
     *
     * @param io_context Boost.Asio event loop (must outlive this server)
     * @param port Port to listen on
     */
    HttpServerAsio(asio::io_context& io_context, uint16_t port);

    /**
     * @brief Set the request handler function
     *
     * Handler will be called from io_context thread(s).
     *
     * @param handler Function to handle requests
     */
    void set_handler(HttpRequestHandler handler);

    /**
     * @brief Get the listening port
     */
    uint16_t get_port() const { return port_; }

private:
    /**
     * @brief Async accept handler
     *
     * Waits for incoming connections and creates HttpConnection objects.
     * Recursively calls itself to continue accepting connections.
     */
    void do_accept();

    tcp::acceptor acceptor_;                // Async acceptor
    asio::io_context& io_context_;          // Event loop reference
    HttpRequestHandler handler_;            // User-provided handler
    uint16_t port_;                         // Listening port
};

} // namespace network
} // namespace dfs
