/**
 * @file http_server_example.cpp
 * @brief Example HTTP server demonstrating Phase 1 implementation
 *
 * This example shows how to create a simple HTTP server that handles
 * different routes and methods. It's a complete working example that
 * can be tested with curl or a web browser.
 *
 * What you'll learn from this example:
 * 1. How to set up an HTTP server
 * 2. How to handle different routes
 * 3. How to parse request bodies
 * 4. How to set response headers
 * 5. How to handle errors gracefully
 */

#include "dfs/network/http_server.hpp"
#include <spdlog/spdlog.h>
#include <iostream>
#include <sstream>
#include <csignal>

using namespace dfs::network;

// Global server pointer for signal handling
dfs::network::HttpServer* g_server = nullptr;

/**
 * @brief Signal handler for graceful shutdown
 *
 * Allows the server to be stopped with Ctrl+C (SIGINT).
 * In production, you'd also handle SIGTERM for Docker/systemd.
 */
void signal_handler(int signal) {
    if (signal == SIGINT) {
        spdlog::info("Received SIGINT, shutting down...");
        if (g_server) {
            g_server->stop();
        }
    }
}

/**
 * @brief Main request handler
 *
 * This function is called for every HTTP request. It acts as a simple
 * router, dispatching to different handlers based on the URL path.
 *
 * Learning note: This is a basic routing implementation. Production
 * servers use more sophisticated routing with:
 * - Pattern matching (e.g., /users/:id)
 * - Middleware chains
 * - Route grouping
 * - Method-specific handlers
 */
HttpResponse handle_request(const HttpRequest& request) {
    // Route 1: Root path - serve a welcome page
    if (request.url == "/" && request.method == HttpMethod::GET) {
        HttpResponse response(HttpStatus::OK);

        std::string html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>DFS HTTP Server - Phase 1</title>
    <style>
        body { font-family: Arial, sans-serif; max-width: 800px; margin: 50px auto; }
        h1 { color: #333; }
        code { background: #f4f4f4; padding: 2px 6px; border-radius: 3px; }
        .endpoint { margin: 20px 0; padding: 15px; background: #f9f9f9; border-left: 4px solid #007bff; }
    </style>
</head>
<body>
    <h1>Welcome to DFS HTTP Server!</h1>
    <p>This is a Phase 1 HTTP/1.1 server built from scratch in C++.</p>

    <h2>Available Endpoints:</h2>

    <div class="endpoint">
        <h3>GET /</h3>
        <p>This welcome page</p>
    </div>

    <div class="endpoint">
        <h3>GET /hello</h3>
        <p>Simple greeting message</p>
        <code>curl http://localhost:8080/hello</code>
    </div>

    <div class="endpoint">
        <h3>GET /info</h3>
        <p>Server information in JSON format</p>
        <code>curl http://localhost:8080/info</code>
    </div>

    <div class="endpoint">
        <h3>POST /echo</h3>
        <p>Echoes back the request body</p>
        <code>curl -X POST http://localhost:8080/echo -d "Hello, Server!"</code>
    </div>

    <div class="endpoint">
        <h3>GET /headers</h3>
        <p>Display all request headers</p>
        <code>curl http://localhost:8080/headers</code>
    </div>

    <h2>Testing with curl:</h2>
    <pre>
# Simple GET request
curl http://localhost:8080/hello

# POST with data
curl -X POST http://localhost:8080/echo -d "test data"

# View headers
curl -v http://localhost:8080/headers
    </pre>
</body>
</html>
)";

        response.set_body(html);
        response.set_header("Content-Type", "text/html");
        return response;
    }

    // Route 2: Simple text response
    if (request.url == "/hello" && request.method == HttpMethod::GET) {
        HttpResponse response(HttpStatus::OK);
        response.set_body("Hello from DFS HTTP Server!\n");
        response.set_header("Content-Type", "text/plain");
        return response;
    }

    // Route 3: JSON response with server info
    if (request.url == "/info" && request.method == HttpMethod::GET) {
        HttpResponse response(HttpStatus::OK);

        // Build JSON manually (we'll add proper JSON library in later phases)
        std::ostringstream json;
        json << "{\n";
        json << "  \"server\": \"DFS HTTP Server\",\n";
        json << "  \"version\": \"1.0.0\",\n";
        json << "  \"phase\": \"Phase 1 - HTTP Implementation\",\n";
        json << "  \"features\": [\n";
        json << "    \"HTTP/1.1 parsing\",\n";
        json << "    \"GET/POST support\",\n";
        json << "    \"Custom routing\",\n";
        json << "    \"Header handling\"\n";
        json << "  ]\n";
        json << "}\n";

        response.set_body(json.str());
        response.set_header("Content-Type", "application/json");
        return response;
    }

    // Route 4: Echo endpoint - returns request body
    if (request.url == "/echo") {
        HttpResponse response(HttpStatus::OK);

        if (request.method == HttpMethod::GET) {
            // Browser GET request - show instructions
            response.set_body("Echo endpoint is ready!\n\nTo test:\ncurl -X POST http://localhost:8080/echo -d \"Your message here\"\n");
        } else if (request.method == HttpMethod::POST) {
            if (request.body.empty()) {
                response.set_body("No body received. Send data with: curl -X POST -d 'data' ...\n");
            } else {
                // Echo back the body
                std::string body_str = request.body_as_string();
                response.set_body("You sent: " + body_str + "\n");
            }
        } else {
            response = HttpResponse(HttpStatus::METHOD_NOT_ALLOWED);
            response.set_body("Method not allowed. Use GET or POST.\n");
        }

        response.set_header("Content-Type", "text/plain");
        return response;
    }

    // Route 5: Display request headers
    if (request.url == "/headers" && request.method == HttpMethod::GET) {
        HttpResponse response(HttpStatus::OK);

        std::ostringstream body;
        body << "Request Headers:\n";
        body << "=================\n\n";

        for (const auto& [name, value] : request.headers) {
            body << name << ": " << value << "\n";
        }

        response.set_body(body.str());
        response.set_header("Content-Type", "text/plain");
        return response;
    }

    // Route not found
    HttpResponse response(HttpStatus::NOT_FOUND);
    std::string html = R"(
<!DOCTYPE html>
<html>
<head><title>404 Not Found</title></head>
<body>
    <h1>404 - Not Found</h1>
    <p>The requested URL was not found on this server.</p>
    <p><a href="/">Go to home page</a></p>
</body>
</html>
)";
    response.set_body(html);
    response.set_header("Content-Type", "text/html");
    return response;
}

int main(int argc, char* argv[]) {
    // Configure logging
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");

    // Parse command line arguments
    uint16_t port = 8080;
    if (argc > 1) {
        try {
            port = static_cast<uint16_t>(std::stoi(argv[1]));
        } catch (...) {
            spdlog::error("Invalid port number: {}", argv[1]);
            std::cerr << "Usage: " << argv[0] << " [port]\n";
            return 1;
        }
    }

    spdlog::info("=================================");
    spdlog::info("DFS HTTP Server Example - Phase 1");
    spdlog::info("=================================");

    // Create server
    HttpServer server;
    g_server = &server;

    // Set up signal handler for graceful shutdown
    std::signal(SIGINT, signal_handler);

    // Set request handler
    server.set_handler(handle_request);

    // Start listening
    auto listen_result = server.listen(port);
    if (listen_result.is_error()) {
        spdlog::error("Failed to start server: {}", listen_result.error());
        return 1;
    }

    spdlog::info("");
    spdlog::info("Server started successfully!");
    spdlog::info("Access the server at: http://localhost:{}", port);
    spdlog::info("");
    spdlog::info("Test with curl:");
    spdlog::info("  curl http://localhost:{}/hello", port);
    spdlog::info("  curl http://localhost:{}/info", port);
    spdlog::info("  curl -X POST http://localhost:{}/echo -d 'Hello!'", port);
    spdlog::info("");
    spdlog::info("Press Ctrl+C to stop");
    spdlog::info("");

    // Run server (blocks until stopped)
    auto serve_result = server.serve_forever();
    if (serve_result.is_error()) {
        spdlog::error("Server error: {}", serve_result.error());
        return 1;
    }

    spdlog::info("Server shut down cleanly");
    return 0;
}