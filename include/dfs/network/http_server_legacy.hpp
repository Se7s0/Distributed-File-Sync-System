#pragma once

#include "socket.hpp"
#include "http_parser.hpp"
#include "http_types.hpp"
#include "dfs/core/result.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace dfs {
namespace network {

/**
 * @brief HTTP request handler function type
 *
 * This is the signature for request handler functions. Users of HttpServer
 * provide a function with this signature to handle incoming requests.
 *
 * Example:
 * ```cpp
 * HttpResponse my_handler(const HttpRequest& request) {
 *     HttpResponse response(HttpStatus::OK);
 *     response.set_body("Hello, World!");
 *     response.set_header("Content-Type", "text/plain");
 *     return response;
 * }
 * ```
 *
 * Learning note: std::function is a type-erased wrapper that can hold
 * any callable (function, lambda, functor) with matching signature.
 * This provides flexibility - users can use lambdas, free functions,
 * or member functions.
 */
using HttpRequestHandler = std::function<HttpResponse(const HttpRequest&)>;

/**
 * @brief LEGACY: Simple single-threaded HTTP/1.1 server
 *
 * ⚠️ This is the legacy implementation - single-threaded blocking I/O.
 * For production use:
 * - Use HttpServer (thread pool) for moderate loads
 * - Use HttpServerAsio (event-driven) for high loads
 *
 * This server handles HTTP requests synchronously - one request at a time.
 *
 * Key features:
 * - HTTP/1.1 compliant parsing
 * - Customizable request handler
 * - Graceful error handling
 * - Connection keep-alive (basic support)
 *
 * Limitations:
 * - Single-threaded (blocks on each request)
 * - No chunked transfer encoding
 * - No compression
 * - No SSL/TLS
 *
 * Usage:
 * ```cpp
 * HttpServerLegacy server;
 * server.set_handler([](const HttpRequest& req) {
 *     HttpResponse res(HttpStatus::OK);
 *     res.set_body("Hello!");
 *     return res;
 * });
 * auto result = server.listen(8080);
 * if (result.is_ok()) {
 *     server.serve_forever();
 * }
 * ```
 */
class HttpServerLegacy {
public:
    HttpServerLegacy();
    ~HttpServerLegacy();

    // Prevent copying (server owns socket resources)
    HttpServerLegacy(const HttpServerLegacy&) = delete;
    HttpServerLegacy& operator=(const HttpServerLegacy&) = delete;

    // Allow moving
    HttpServerLegacy(HttpServerLegacy&&) noexcept = default;
    HttpServerLegacy& operator=(HttpServerLegacy&&) noexcept = default;

    /**
     * @brief Set the request handler function
     *
     * This function will be called for each incoming HTTP request.
     * It must return an HttpResponse that will be sent back to the client.
     *
     * @param handler Function to handle requests
     */
    void set_handler(HttpRequestHandler handler);

    /**
     * @brief Start listening on a port
     *
     * Binds to the specified port and starts listening for connections.
     * By default, binds to all interfaces (0.0.0.0).
     *
     * @param port Port number to listen on
     * @param address Address to bind to (default: "0.0.0.0" - all interfaces)
     * @return Result indicating success or error
     */
    Result<void> listen(uint16_t port, const std::string& address = "0.0.0.0");

    /**
     * @brief Run the server (blocking)
     *
     * This enters the main server loop and handles connections.
     * It will run forever until stop() is called or an error occurs.
     *
     * Learning note: In a real production server, you'd want:
     * - Thread pool for handling multiple connections concurrently
     * - epoll/IOCP for async I/O
     * - Graceful shutdown mechanism
     * - Health checks and monitoring
     *
     * For Phase 1, we keep it simple and synchronous.
     *
     * @return Result indicating if server stopped cleanly or with error
     */
    Result<void> serve_forever();

    /**
     * @brief Stop the server
     *
     * Signals the server to stop accepting new connections.
     * The serve_forever() loop will exit after the current request finishes.
     */
    void stop();

    /**
     * @brief Check if server is currently running
     */
    bool is_running() const { return running_; }

    /**
     * @brief Get the port the server is listening on
     */
    uint16_t get_port() const { return port_; }

private:
    Socket listener_;                 // Listening socket
    HttpRequestHandler handler_;      // User-provided request handler
    bool running_;                    // Server running flag
    uint16_t port_;                   // Port we're listening on

    /**
     * @brief Handle a single client connection
     *
     * This is called for each accepted connection. It:
     * 1. Reads data from the client
     * 2. Parses the HTTP request
     * 3. Calls the user handler
     * 4. Sends the response
     * 5. Closes the connection (or keeps alive for HTTP/1.1)
     *
     * @param client Socket for the client connection
     * @return Result indicating success or error
     */
    Result<void> handle_connection(std::unique_ptr<Socket> client);

    /**
     * @brief Read a complete HTTP request from socket
     *
     * Uses the HttpParser to incrementally parse data as it arrives.
     * Continues reading until the parser indicates completion or error.
     *
     * @param socket Socket to read from
     * @param parser Parser to use
     * @return Result containing the parsed request or error
     */
    Result<HttpRequest> read_request(Socket& socket, HttpParser& parser);

    /**
     * @brief Send HTTP response to client
     *
     * Serializes the response and sends it over the socket.
     *
     * @param socket Socket to send to
     * @param response Response to send
     * @return Result indicating success or error
     */
    Result<void> send_response(Socket& socket, const HttpResponse& response);

    /**
     * @brief Create an error response
     *
     * Helper function to create standard error responses.
     * Used when request parsing fails or handler throws.
     *
     * @param status HTTP status code
     * @param message Error message to include in body
     * @return Error response
     */
    HttpResponse create_error_response(HttpStatus status, const std::string& message);
};

} // namespace network
} // namespace dfs