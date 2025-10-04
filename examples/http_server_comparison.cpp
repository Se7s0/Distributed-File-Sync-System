/**
 * @file http_server_comparison.cpp
 * @brief Demonstrates all three HTTP server implementations
 *
 * This example shows how to use:
 * 1. HttpServerLegacy - Single-threaded (for comparison)
 * 2. HttpServer - Thread pool (Phase 1)
 * 3. HttpServerAsio - Event-driven (Phase 2)
 *
 * ═══════════════════════════════════════════════════════════
 * QUICK CONFIGURATION - CHANGE THESE TO TEST DIFFERENT SERVERS
 * ═══════════════════════════════════════════════════════════
 */

// ┌─────────────────────────────────────────────────────────┐
// │  CHOOSE YOUR SERVER TYPE (uncomment ONE)                │
// └─────────────────────────────────────────────────────────┘

// #define USE_LEGACY_SERVER        // 🔴 Single-threaded (1 request at a time)
//#define USE_THREADPOOL_SERVER    // 🔵 Thread pool (DEFAULT - recommended)
 #define USE_ASIO_SERVER          // 🟢 Event-driven (requires Boost.Asio)

// ┌─────────────────────────────────────────────────────────┐
// │  SERVER CONFIGURATION                                   │
// └─────────────────────────────────────────────────────────┘

#define SERVER_PORT 8080                    // Port to listen on
#define THREAD_POOL_SIZE 8                  // Number of worker threads (for thread pool mode)
#define LOG_LEVEL spdlog::level::info       // Logging level (trace, debug, info, warn, error)

// ═══════════════════════════════════════════════════════════

#include "dfs/network/http_server.hpp"           // Thread pool version
#include "dfs/network/http_server_legacy.hpp"    // Legacy single-threaded
#ifdef DFS_HAS_BOOST_ASIO
#include "dfs/network/http_server_asio.hpp"      // Asio event-driven
#endif
#include <spdlog/spdlog.h>
#include <iostream>
#include <sstream>
#include <csignal>
#include <memory>

using namespace dfs::network;

// Global pointers for signal handling
HttpServer* g_server_threadpool = nullptr;
HttpServerLegacy* g_server_legacy = nullptr;
#ifdef DFS_HAS_BOOST_ASIO
boost::asio::io_context* g_io_context = nullptr;
#endif

void signal_handler(int signal) {
    if (signal == SIGINT) {
        spdlog::info("Received SIGINT, shutting down...");

        if (g_server_threadpool) {
            g_server_threadpool->stop();
        }
        if (g_server_legacy) {
            g_server_legacy->stop();
        }
#ifdef DFS_HAS_BOOST_ASIO
        if (g_io_context) {
            g_io_context->stop();
        }
#endif
    }
}

/**
 * @brief Request handler - works with all three server types!
 *
 * This demonstrates code reuse - the same handler works for
 * legacy, thread pool, and Asio servers.
 */
HttpResponse handle_request(const HttpRequest& request) {
    // Root path - welcome page
    if (request.url == "/" && request.method == HttpMethod::GET) {
        HttpResponse response(HttpStatus::OK);

        std::string html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>DFS HTTP Server Comparison</title>
    <style>
        body { font-family: Arial; max-width: 900px; margin: 50px auto; }
        .server-type { padding: 20px; margin: 20px 0; border-radius: 8px; }
        .legacy { background: #ffe6e6; border-left: 5px solid #cc0000; }
        .threadpool { background: #e6f3ff; border-left: 5px solid #0066cc; }
        .asio { background: #e6ffe6; border-left: 5px solid #00cc00; }
        code { background: #f4f4f4; padding: 2px 6px; border-radius: 3px; }
    </style>
</head>
<body>
    <h1>HTTP Server Implementations</h1>

    <div class="server-type legacy">
        <h2>🔴 HttpServerLegacy (Single-threaded)</h2>
        <p><strong>Use for:</strong> Learning, simple prototypes</p>
        <p><strong>Max connections:</strong> 1 at a time</p>
        <p><strong>Slowloris vulnerable:</strong> Yes</p>
    </div>

    <div class="server-type threadpool">
        <h2>🔵 HttpServer (Thread Pool)</h2>
        <p><strong>Use for:</strong> Production, moderate loads (10-500 concurrent)</p>
        <p><strong>Max connections:</strong> Thread pool size (default: 2x CPU cores)</p>
        <p><strong>Slowloris vulnerable:</strong> Yes (but mitigated)</p>
    </div>

    <div class="server-type asio">
        <h2>🟢 HttpServerAsio (Event-Driven)</h2>
        <p><strong>Use for:</strong> High performance, high loads (1000+ concurrent)</p>
        <p><strong>Max connections:</strong> 10,000+</p>
        <p><strong>Slowloris vulnerable:</strong> No</p>
    </div>

    <h2>Test Endpoints:</h2>
    <ul>
        <li><code>GET /</code> - This page</li>
        <li><code>GET /hello</code> - Simple greeting</li>
        <li><code>GET /stats</code> - Server statistics</li>
        <li><code>POST /echo</code> - Echo request body</li>
    </ul>

    <h2>Quick Test:</h2>
    <pre>
# Test with curl
curl http://localhost:8080/hello
curl http://localhost:8080/stats
curl -X POST http://localhost:8080/echo -d "Hello, Server!"
    </pre>
</body>
</html>
)";

        response.set_body(html);
        response.set_header("Content-Type", "text/html; charset=utf-8");
        return response;
    }

    // Simple text endpoint
    if (request.url == "/hello" && request.method == HttpMethod::GET) {
        HttpResponse response(HttpStatus::OK);
        response.set_body("Hello from DFS HTTP Server!\n");
        response.set_header("Content-Type", "text/plain");
        return response;
    }

    // Server statistics (only works with thread pool version)
    if (request.url == "/stats" && request.method == HttpMethod::GET) {
        HttpResponse response(HttpStatus::OK);

        std::ostringstream json;
        json << "{\n";
        json << "  \"server\": \"DFS HTTP Server\",\n";
        json << "  \"endpoints\": {\n";
        json << "    \"GET /\": \"Welcome page\",\n";
        json << "    \"GET /hello\": \"Simple greeting\",\n";
        json << "    \"GET /stats\": \"This page\",\n";
        json << "    \"POST /echo\": \"Echo request body\"\n";
        json << "  }\n";

        // Thread pool stats (only available if using HttpServer)
        if (g_server_threadpool) {
            json << ",\n  \"threadpool_stats\": {\n";
            json << "    \"active_connections\": " << g_server_threadpool->get_active_connections() << ",\n";
            json << "    \"total_processed\": " << g_server_threadpool->get_total_processed() << "\n";
            json << "  }\n";
        }

        json << "}\n";

        response.set_body(json.str());
        response.set_header("Content-Type", "application/json");
        return response;
    }

    // Echo endpoint
    if (request.url == "/echo") {
        HttpResponse response(HttpStatus::OK);

        if (request.method == HttpMethod::POST) {
            if (request.body.empty()) {
                response.set_body("No body received. Send data with: curl -X POST -d 'data' ...\n");
            } else {
                std::string body_str = request.body_as_string();
                response.set_body("You sent: " + body_str + "\n");
            }
        } else {
            response.set_body("Use POST method. Example: curl -X POST http://localhost:8080/echo -d 'message'\n");
        }

        response.set_header("Content-Type", "text/plain");
        return response;
    }

    // 404 Not Found
    HttpResponse response(HttpStatus::NOT_FOUND);
    response.set_body("404 - Not Found\n\nAvailable endpoints: /, /hello, /stats, /echo\n");
    response.set_header("Content-Type", "text/plain");
    return response;
}

/**
 * @brief Run HttpServerLegacy (single-threaded)
 */
int run_legacy_server(uint16_t port) {
    spdlog::info("Starting LEGACY single-threaded server...");

    HttpServerLegacy server;
    g_server_legacy = &server;

    server.set_handler(handle_request);

    auto listen_result = server.listen(port);
    if (listen_result.is_error()) {
        spdlog::error("Failed to start server: {}", listen_result.error());
        return 1;
    }

    spdlog::info("🔴 Legacy server running on http://localhost:{}", port);
    spdlog::info("Note: This version handles ONE request at a time");

    auto serve_result = server.serve_forever();
    if (serve_result.is_error()) {
        spdlog::error("Server error: {}", serve_result.error());
        return 1;
    }

    return 0;
}

/**
 * @brief Run HttpServer (thread pool)
 */
int run_threadpool_server(uint16_t port, size_t num_threads) {
    spdlog::info("Starting THREAD POOL server...");

    HttpServer server(num_threads);
    g_server_threadpool = &server;

    server.set_handler(handle_request);

    auto listen_result = server.listen(port);
    if (listen_result.is_error()) {
        spdlog::error("Failed to start server: {}", listen_result.error());
        return 1;
    }

    spdlog::info("🔵 Thread pool server running on http://localhost:{}", port);
    spdlog::info("Worker threads: {}", num_threads);
    spdlog::info("Can handle {} concurrent requests", num_threads);

    auto serve_result = server.serve_forever();
    if (serve_result.is_error()) {
        spdlog::error("Server error: {}", serve_result.error());
        return 1;
    }

    return 0;
}

#ifdef DFS_HAS_BOOST_ASIO
/**
 * @brief Run HttpServerAsio (event-driven)
 */
int run_asio_server(uint16_t port) {
    spdlog::info("Starting ASIO event-driven server...");

    boost::asio::io_context io_context;
    g_io_context = &io_context;

    HttpServerAsio server(io_context, port);
    server.set_handler(handle_request);

    spdlog::info("🟢 Asio server running on http://localhost:{}", port);
    spdlog::info("Event-driven I/O - can handle 10,000+ connections");
    spdlog::info("Press Ctrl+C to stop");

    // Run event loop (blocks here)
    io_context.run();

    return 0;
}
#endif

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --legacy              Use single-threaded legacy server\n";
    std::cout << "  --threadpool [N]      Use thread pool server with N threads (default: auto)\n";
#ifdef DFS_HAS_BOOST_ASIO
    std::cout << "  --asio                Use Boost.Asio event-driven server\n";
#endif
    std::cout << "  --port PORT           Port to listen on (default: 8080)\n";
    std::cout << "  --help                Show this help message\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program_name << " --threadpool          # Thread pool with auto threads\n";
    std::cout << "  " << program_name << " --threadpool 8       # Thread pool with 8 threads\n";
#ifdef DFS_HAS_BOOST_ASIO
    std::cout << "  " << program_name << " --asio               # Event-driven Asio server\n";
#endif
    std::cout << "  " << program_name << " --legacy --port 9000 # Legacy server on port 9000\n";
}

int main(int argc, char* argv[]) {
    // Configure logging from defines
    spdlog::set_level(LOG_LEVEL);
    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");

    // Determine mode from compile-time defines OR command-line args
    std::string mode;
    uint16_t port = SERVER_PORT;
    size_t num_threads = THREAD_POOL_SIZE;

    // Check compile-time configuration
#if defined(USE_LEGACY_SERVER)
    mode = "legacy";
#elif defined(USE_THREADPOOL_SERVER)
    mode = "threadpool";
#elif defined(USE_ASIO_SERVER)
    mode = "asio";
#else
    // No define set - default to thread pool
    mode = "threadpool";
#endif

    // Command-line args override compile-time settings
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--legacy") {
            mode = "legacy";
        } else if (arg == "--threadpool") {
            mode = "threadpool";
            // Check if next arg is thread count
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                try {
                    num_threads = std::stoul(argv[++i]);
                } catch (...) {
                    spdlog::error("Invalid thread count: {}", argv[i]);
                    return 1;
                }
            }
        } else if (arg == "--asio") {
#ifdef DFS_HAS_BOOST_ASIO
            mode = "asio";
#else
            spdlog::error("Asio support not compiled in. Build with Boost.Asio.");
            return 1;
#endif
        } else if (arg == "--port") {
            if (i + 1 < argc) {
                try {
                    port = static_cast<uint16_t>(std::stoi(argv[++i]));
                } catch (...) {
                    spdlog::error("Invalid port: {}", argv[i]);
                    return 1;
                }
            } else {
                spdlog::error("--port requires a value");
                return 1;
            }
        } else {
            spdlog::error("Unknown option: {}", arg);
            print_usage(argv[0]);
            return 1;
        }
    }

    spdlog::info("====================================");
    spdlog::info("DFS HTTP Server - Implementation Demo");
    spdlog::info("====================================");
    spdlog::info("");
    spdlog::info("Configuration:");
    spdlog::info("  Mode: {}", mode);
    spdlog::info("  Port: {}", port);
    if (mode == "threadpool") {
        spdlog::info("  Worker threads: {}", num_threads);
    }
    spdlog::info("");

    // Set up signal handler
    std::signal(SIGINT, signal_handler);

    // Run selected server
    int result = 0;

    if (mode == "legacy") {
        result = run_legacy_server(port);
    } else if (mode == "threadpool") {
        result = run_threadpool_server(port, num_threads);
    }
#ifdef DFS_HAS_BOOST_ASIO
    else if (mode == "asio") {
        result = run_asio_server(port);
    }
#endif
    else {
        spdlog::error("Invalid mode: {}", mode);
        return 1;
    }

    spdlog::info("Server shut down cleanly");
    return result;
}
