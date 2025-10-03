#include "dfs/network/http_server.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace dfs {
namespace network {

using dfs::Ok;
using dfs::Err;

HttpServer::HttpServer()
    : running_(false)
    , port_(0) {
}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::set_handler(HttpRequestHandler handler) {
    handler_ = std::move(handler);
}

Result<void> HttpServer::listen(uint16_t port, const std::string& address) {
    // Create TCP socket
    auto create_result = listener_.create(SocketType::TCP);
    if (create_result.is_error()) {
        return Err<void, std::string>("Failed to create listener socket: " + create_result.error());
    }

    // Set socket options for better behavior
    auto reuse_result = listener_.set_reuse_address(true);
    if (reuse_result.is_error()) {
        spdlog::warn("Failed to set SO_REUSEADDR: {}", reuse_result.error());
        // Non-fatal, continue
    }

    // Bind to address and port
    auto bind_result = listener_.bind(address, port);
    if (bind_result.is_error()) {
        return Err<void, std::string>("Failed to bind to " + address + ":" +
                       std::to_string(port) + " - " + bind_result.error());
    }

    // Start listening for connections
    auto listen_result = listener_.listen(128); // backlog of 128 connections
    if (listen_result.is_error()) {
        return Err<void, std::string>("Failed to listen: " + listen_result.error());
    }

    port_ = port;
    spdlog::info("HTTP server listening on {}:{}", address, port);

    return Ok();
}

Result<void> HttpServer::serve_forever() {
    if (!listener_.is_valid()) {
        return Err<void, std::string>("Server not initialized. Call listen() first.");
    }

    if (!handler_) {
        return Err<void, std::string>("No request handler set. Call set_handler() first.");
    }

    running_ = true;
    spdlog::info("Server started. Waiting for connections...");

    // Main server loop
    while (running_) {
        // Accept incoming connection
        // Note: This blocks until a connection arrives
        auto accept_result = listener_.accept();
        if (accept_result.is_error()) {
            if (!running_) {
                // Server was stopped, exit gracefully
                break;
            }
            spdlog::error("Failed to accept connection: {}", accept_result.error());
            continue; // Continue accepting other connections
        }

        auto client = std::move(accept_result.value());
        spdlog::debug("Accepted new connection");

        // Handle the connection
        auto handle_result = handle_connection(std::move(client));
        if (handle_result.is_error()) {
            spdlog::error("Error handling connection: {}", handle_result.error());
            // Continue with next connection
        }
    }

    spdlog::info("Server stopped");
    return Ok();
}

void HttpServer::stop() {
    if (running_) {
        spdlog::info("Stopping server...");
        running_ = false;
        listener_.close();
    }
}

Result<void> HttpServer::handle_connection(std::unique_ptr<Socket> client) {
    // Create parser for this connection
    HttpParser parser;

    // Read and parse the request
    auto request_result = read_request(*client, parser);
    if (request_result.is_error()) {
        // Send error response
        auto error_response = create_error_response(
            HttpStatus::BAD_REQUEST,
            "Failed to parse request: " + request_result.error()
        );
        send_response(*client, error_response);
        return Err<void, std::string>(request_result.error());
    }

    HttpRequest request = request_result.value();

    // Log the request
    spdlog::info("{} {} HTTP/{}",
                HttpMethodUtils::to_string(request.method),
                request.url,
                request.version == HttpVersion::HTTP_1_1 ? "1.1" : "1.0");

    // Call user handler
    HttpResponse response;
    try {
        response = handler_(request);
    } catch (const std::exception& e) {
        spdlog::error("Handler threw exception: {}", e.what());
        response = create_error_response(
            HttpStatus::INTERNAL_SERVER_ERROR,
            "Internal server error"
        );
    } catch (...) {
        spdlog::error("Handler threw unknown exception");
        response = create_error_response(
            HttpStatus::INTERNAL_SERVER_ERROR,
            "Internal server error"
        );
    }

    // Send response
    auto send_result = send_response(*client, response);
    if (send_result.is_error()) {
        return Err<void, std::string>("Failed to send response: " + send_result.error());
    }

    // Check for Connection: keep-alive header
    // For Phase 1, we always close the connection after each request
    // In later phases, we'll implement proper keep-alive support
    client->close();

    return Ok();
}

Result<HttpRequest> HttpServer::read_request(Socket& socket, HttpParser& parser) {
    // Buffer for reading data
    constexpr size_t BUFFER_SIZE = 4096;

    // Read data in chunks until request is complete
    while (!parser.is_complete()) {
        // Read data from socket
        auto recv_result = socket.receive(BUFFER_SIZE);
        if (recv_result.is_error()) {
            return Err<HttpRequest, std::string>("Failed to read from socket: " + recv_result.error());
        }

        const auto& data = recv_result.value();
        if (data.empty()) {
            // Client closed connection
            return Err<HttpRequest, std::string>("Client closed connection before sending complete request");
        }

        // Feed data to parser
        auto parse_result = parser.parse(
            reinterpret_cast<const char*>(data.data()),
            data.size()
        );

        if (parse_result.is_error()) {
            return Err<HttpRequest, std::string>(parse_result.error());
        }

        if (parse_result.value()) {
            // Parsing complete!
            break;
        }

        // Need more data, continue loop
    }

    return Ok(parser.get_request());
}

Result<void> HttpServer::send_response(Socket& socket, const HttpResponse& response) {
    // Serialize response to bytes
    std::vector<uint8_t> data = response.serialize();

    // Send all data
    // Note: We might need multiple send() calls if the buffer is large
    size_t total_sent = 0;
    while (total_sent < data.size()) {
        // Create a view of remaining data
        std::vector<uint8_t> remaining(
            data.begin() + total_sent,
            data.end()
        );

        auto send_result = socket.send(remaining);
        if (send_result.is_error()) {
            return Err<void, std::string>("Failed to send response: " + send_result.error());
        }

        total_sent += send_result.value();
    }

    spdlog::debug("Sent {} bytes", total_sent);
    return Ok();
}

HttpResponse HttpServer::create_error_response(HttpStatus status, const std::string& message) {
    HttpResponse response(status);

    // Create simple HTML error page
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