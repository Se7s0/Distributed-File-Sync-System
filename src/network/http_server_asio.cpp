#include "dfs/network/http_server_asio.hpp"
#include <spdlog/spdlog.h>

namespace dfs {
namespace network {

// ──────────────────────────────────────────────────────────
// HttpConnection Implementation
// ──────────────────────────────────────────────────────────

HttpConnection::HttpConnection(tcp::socket socket, HttpRequestHandler handler)
    : socket_(std::move(socket))
    , handler_(std::move(handler))
    , parser_() {
}

void HttpConnection::start() {
    do_read();
}

void HttpConnection::do_read() {
    auto self = shared_from_this();  // Keep connection alive during async operation

    // Async read - returns immediately, callback invoked when data arrives
    socket_.async_read_some(
        asio::buffer(buffer_),
        [this, self](boost::system::error_code ec, size_t bytes_transferred) {
            if (!ec) {
                // ────────────────────────────────────────
                // REUSED: Exact same parsing logic from legacy server
                // (http_server_legacy.cpp lines 180-188)
                // ────────────────────────────────────────
                auto parse_result = parser_.parse(
                    buffer_.data(),
                    bytes_transferred
                );

                if (parse_result.is_error()) {
                    handle_error("Parse error: " + parse_result.error());
                    return;
                }

                if (parse_result.value()) {
                    // ────────────────────────────────────────
                    // REUSED: Same request handling logic
                    // (http_server_legacy.cpp lines 122-146)
                    // ────────────────────────────────────────
                    HttpRequest request = parser_.get_request();

                    spdlog::info("{} {} HTTP/{}",
                        HttpMethodUtils::to_string(request.method),
                        request.url,
                        request.version == HttpVersion::HTTP_1_1 ? "1.1" : "1.0");

                    HttpResponse response;
                    try {
                        response = handler_(request);  // Call user handler
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

                    do_write(response);
                } else {
                    // Need more data - continue reading
                    do_read();
                }
            } else if (ec != asio::error::operation_aborted) {
                spdlog::debug("Read error: {}", ec.message());
            }
        }
    );
}

void HttpConnection::do_write(const HttpResponse& response) {
    auto self = shared_from_this();  // Keep connection alive during async write

    // ────────────────────────────────────────
    // REUSED: Exact same serialization from legacy server
    // (http_server_legacy.cpp line 203)
    // ────────────────────────────────────────
    std::vector<uint8_t> data = response.serialize();

    // Store data in shared_ptr so it stays alive during async operation
    auto data_ptr = std::make_shared<std::vector<uint8_t>>(std::move(data));

    // Async write - returns immediately, callback invoked when write completes
    asio::async_write(
        socket_,
        asio::buffer(*data_ptr),
        [this, self, data_ptr](boost::system::error_code ec, size_t bytes_transferred) {
            if (!ec) {
                spdlog::debug("Sent {} bytes", bytes_transferred);

                // Gracefully shutdown the socket
                boost::system::error_code shutdown_ec;
                socket_.shutdown(tcp::socket::shutdown_both, shutdown_ec);

                // Connection will be destroyed when this lambda exits
                // (no more shared_ptr references)
            } else if (ec != asio::error::operation_aborted) {
                spdlog::debug("Write error: {}", ec.message());
            }
        }
    );
}

void HttpConnection::handle_error(const std::string& message) {
    // ────────────────────────────────────────
    // REUSED: Same error handling logic from legacy server
    // (http_server_legacy.cpp lines 114-119)
    // ────────────────────────────────────────
    spdlog::warn("Connection error: {}", message);

    auto error_response = create_error_response(
        HttpStatus::BAD_REQUEST,
        message
    );

    do_write(error_response);
}

// ────────────────────────────────────────
// REUSED: EXACT COPY from http_server_legacy.cpp lines 227-246
// ────────────────────────────────────────
HttpResponse HttpConnection::create_error_response(HttpStatus status, const std::string& message) {
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

// ──────────────────────────────────────────────────────────
// HttpServerAsio Implementation
// ──────────────────────────────────────────────────────────

HttpServerAsio::HttpServerAsio(asio::io_context& io_context, uint16_t port)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
    , io_context_(io_context)
    , port_(port) {

    spdlog::info("HTTP server (Asio event-driven) listening on port {}", port);

    // Start accepting connections
    do_accept();
}

void HttpServerAsio::set_handler(HttpRequestHandler handler) {
    // ────────────────────────────────────────
    // REUSED: Exact same from http_server_legacy.cpp line 20-22
    // ────────────────────────────────────────
    handler_ = std::move(handler);
}

void HttpServerAsio::do_accept() {
    // Async accept - returns immediately, callback invoked when client connects
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                spdlog::debug("Accepted new connection (Asio)");

                // Create connection handler (replaces direct handle_connection call)
                std::make_shared<HttpConnection>(
                    std::move(socket),
                    handler_
                )->start();
            } else {
                spdlog::error("Accept error: {}", ec.message());
            }

            // Accept next connection (recursive - keeps server accepting)
            do_accept();
        }
    );
}

} // namespace network
} // namespace dfs
