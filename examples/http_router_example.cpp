/**
 * @file http_router_example.cpp
 * @brief Demonstrates HTTP router with organized endpoints for DFS API
 *
 * This example shows how to use HttpRouter to organize routes like a
 * production REST API. This is what you'll use in Phase 2+!
 *
 * Run with:
 *   ./build/examples/http_router_example
 *
 * Test with:
 *   curl http://localhost:8080/
 *   curl http://localhost:8080/api/health
 *   curl http://localhost:8080/api/users/123
 *   curl -X POST http://localhost:8080/api/sync/start -d '{"file":"test.txt"}'
 */

#include "dfs/network/http_server.hpp"
#include "dfs/network/http_router.hpp"
#include <spdlog/spdlog.h>
#include <csignal>
#include <sstream>
#include <nlohmann/json.hpp>

using namespace dfs::network;
using json = nlohmann::json;

// Global server pointer for signal handling
HttpServer* g_server = nullptr;

void signal_handler(int signal) {
    if (signal == SIGINT) {
        spdlog::info("Received SIGINT, shutting down...");
        if (g_server) {
            g_server->stop();
        }
    }
}

// ════════════════════════════════════════════════════════════
// Homepage Routes
// ════════════════════════════════════════════════════════════

HttpResponse serve_homepage(const HttpContext& ctx) {
    HttpResponse response(HttpStatus::OK);

    std::string html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>DFS HTTP Router Example</title>
    <style>
        body { font-family: Arial; max-width: 900px; margin: 50px auto; }
        h1 { color: #333; }
        .endpoint { background: #f4f4f4; padding: 10px; margin: 10px 0; border-left: 4px solid #0066cc; }
        code { background: #eee; padding: 2px 6px; border-radius: 3px; }
    </style>
</head>
<body>
    <h1>🎯 HTTP Router Example - Phase 1.5</h1>
    <p>This demonstrates organized routing ready for Phase 2 (Metadata & Sync API)</p>

    <h2>Available Endpoints:</h2>

    <div class="endpoint">
        <strong>GET /</strong><br>
        This page
    </div>

    <div class="endpoint">
        <strong>GET /api/health</strong><br>
        Health check endpoint<br>
        <code>curl http://localhost:8080/api/health</code>
    </div>

    <div class="endpoint">
        <strong>GET /api/users/:id</strong><br>
        Get user by ID (demonstrates URL parameters)<br>
        <code>curl http://localhost:8080/api/users/123</code>
    </div>

    <div class="endpoint">
        <strong>POST /api/register</strong><br>
        Register new client (Phase 2 preview)<br>
        <code>curl -X POST http://localhost:8080/api/register -d '{"client_id":"laptop1"}'</code>
    </div>

    <div class="endpoint">
        <strong>POST /api/sync/start</strong><br>
        Start sync session (Phase 2 preview)<br>
        <code>curl -X POST http://localhost:8080/api/sync/start -d '{"file":"test.txt"}'</code>
    </div>

    <div class="endpoint">
        <strong>GET /api/sync/status/:session_id</strong><br>
        Get sync session status<br>
        <code>curl http://localhost:8080/api/sync/status/abc123</code>
    </div>

    <div class="endpoint">
        <strong>POST /api/file/upload</strong><br>
        Upload file chunk<br>
        <code>curl -X POST http://localhost:8080/api/file/upload -d 'file content'</code>
    </div>

    <div class="endpoint">
        <strong>GET /api/file/download/:filename</strong><br>
        Download file<br>
        <code>curl http://localhost:8080/api/file/download/test.txt</code>
    </div>

    <h2>Middleware Examples:</h2>
    <p>All requests are logged automatically via middleware!</p>
    <p>Try accessing any endpoint and watch the server logs.</p>

    <hr>
    <p><em>Phase 1.5 - Router System Complete</em></p>
</body>
</html>
)";

    response.set_body(html);
    response.set_header("Content-Type", "text/html; charset=utf-8");
    return response;
}

// ════════════════════════════════════════════════════════════
// API Routes (Preview of Phase 2)
// ════════════════════════════════════════════════════════════

HttpResponse handle_health(const HttpContext& ctx) {
    json response_json = {
        {"status", "healthy"},
        {"service", "dfs-server"},
        {"version", "1.0.0"},
        {"timestamp", std::time(nullptr)}
    };

    HttpResponse response(HttpStatus::OK);
    response.set_body(response_json.dump(2));
    response.set_header("Content-Type", "application/json");
    return response;
}

HttpResponse handle_get_user(const HttpContext& ctx) {
    // Extract URL parameter
    std::string user_id = ctx.get_param("id");

    json response_json = {
        {"user_id", user_id},
        {"username", "user_" + user_id},
        {"registered", "2024-01-15T10:30:00Z"},
        {"files_synced", 42}
    };

    HttpResponse response(HttpStatus::OK);
    response.set_body(response_json.dump(2));
    response.set_header("Content-Type", "application/json");
    return response;
}

HttpResponse handle_register(const HttpContext& ctx) {
    // Parse request body
    std::string body = ctx.request.body_as_string();

    json response_json = {
        {"status", "registered"},
        {"message", "Client registered successfully"},
        {"received_data", body}
    };

    HttpResponse response(HttpStatus::CREATED);
    response.set_body(response_json.dump(2));
    response.set_header("Content-Type", "application/json");
    return response;
}

HttpResponse handle_sync_start(const HttpContext& ctx) {
    std::string body = ctx.request.body_as_string();

    // Generate mock session ID
    std::string session_id = "session_" + std::to_string(std::time(nullptr));

    json response_json = {
        {"status", "started"},
        {"session_id", session_id},
        {"message", "Sync session initiated"},
        {"request_data", body}
    };

    HttpResponse response(HttpStatus::OK);
    response.set_body(response_json.dump(2));
    response.set_header("Content-Type", "application/json");
    return response;
}

HttpResponse handle_sync_status(const HttpContext& ctx) {
    std::string session_id = ctx.get_param("session_id");

    json response_json = {
        {"session_id", session_id},
        {"status", "in_progress"},
        {"progress", 0.75},
        {"files_synced", 150},
        {"files_remaining", 50},
        {"bytes_transferred", 104857600}  // 100 MB
    };

    HttpResponse response(HttpStatus::OK);
    response.set_body(response_json.dump(2));
    response.set_header("Content-Type", "application/json");
    return response;
}

HttpResponse handle_file_upload(const HttpContext& ctx) {
    size_t bytes_received = ctx.request.body.size();

    json response_json = {
        {"status", "uploaded"},
        {"bytes_received", bytes_received},
        {"message", "File chunk received"}
    };

    HttpResponse response(HttpStatus::OK);
    response.set_body(response_json.dump(2));
    response.set_header("Content-Type", "application/json");
    return response;
}

HttpResponse handle_file_download(const HttpContext& ctx) {
    std::string filename = ctx.get_param("filename");

    // Mock file content
    std::string file_content = "This is the content of " + filename + "\n";
    file_content += "In Phase 2, this will return actual file data.\n";

    HttpResponse response(HttpStatus::OK);
    response.set_body(file_content);
    response.set_header("Content-Type", "application/octet-stream");
    response.set_header("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    return response;
}

// ════════════════════════════════════════════════════════════
// Middleware Examples
// ════════════════════════════════════════════════════════════

bool logging_middleware(const HttpContext& ctx, HttpResponse& response) {
    // Log every request
    spdlog::info("{} {} from {}",
        HttpMethodUtils::to_string(ctx.request.method),
        ctx.request.url,
        ctx.request.get_header("User-Agent"));

    return true;  // Continue to handler
}

bool cors_middleware(const HttpContext& ctx, HttpResponse& response) {
    // Add CORS headers (for browser access)
    response.set_header("Access-Control-Allow-Origin", "*");
    response.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    response.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");

    // Handle OPTIONS preflight requests
    if (ctx.request.method == HttpMethod::OPTIONS) {
        response = HttpResponse(HttpStatus::NO_CONTENT);
        return false;  // Stop here, don't call handler
    }

    return true;  // Continue to handler
}

bool auth_middleware_example(const HttpContext& ctx, HttpResponse& response) {
    // Example: Check for authorization header
    // (Not enabled by default - just showing how it would work)

    if (!ctx.request.has_header("Authorization")) {
        spdlog::warn("Unauthorized request to {}", ctx.request.url);

        json error_json = {
            {"error", "unauthorized"},
            {"message", "Authorization header required"}
        };

        response = HttpResponse(HttpStatus::UNAUTHORIZED);
        response.set_body(error_json.dump(2));
        response.set_header("Content-Type", "application/json");
        return false;  // Stop, don't call handler
    }

    return true;  // Continue to handler
}

// ════════════════════════════════════════════════════════════
// Main: Setup Router and Server
// ════════════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    // Configure logging
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");

    // Parse port from command line
    uint16_t port = 8080;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }

    spdlog::info("════════════════════════════════════════════");
    spdlog::info("DFS HTTP Router Example - Phase 1.5");
    spdlog::info("════════════════════════════════════════════");
    spdlog::info("");

    // ────────────────────────────────────────────────────────
    // Setup Router
    // ────────────────────────────────────────────────────────

    HttpRouter router;

    // Add middleware (runs for ALL requests)
    router.use(logging_middleware);
    router.use(cors_middleware);
    // router.use(auth_middleware_example);  // Uncomment to require auth

    // ────────────────────────────────────────────────────────
    // Register Routes
    // ────────────────────────────────────────────────────────

    // Homepage
    router.get("/", serve_homepage);

    // API group
    auto api = router.group("/api");

    // Health check
    api->get("/health", handle_health);

    // User endpoints (demonstrates URL parameters)
    api->get("/users/:id", handle_get_user);

    // Registration (Phase 2 preview)
    api->post("/register", handle_register);

    // Sync endpoints (Phase 2 preview)
    api->post("/sync/start", handle_sync_start);
    api->get("/sync/status/:session_id", handle_sync_status);

    // File endpoints (Phase 2 preview)
    api->post("/file/upload", handle_file_upload);
    api->get("/file/download/:filename", handle_file_download);

    // Custom 404 handler
    router.set_not_found_handler([](const HttpContext& ctx) {
        json error_json = {
            {"error", "not_found"},
            {"message", "The requested endpoint does not exist"},
            {"url", ctx.request.url},
            {"method", HttpMethodUtils::to_string(ctx.request.method)}
        };

        HttpResponse response(HttpStatus::NOT_FOUND);
        response.set_body(error_json.dump(2));
        response.set_header("Content-Type", "application/json");
        return response;
    });

    // ────────────────────────────────────────────────────────
    // List all registered routes
    // ────────────────────────────────────────────────────────

    spdlog::info("Registered routes:");
    for (const auto& route : router.list_routes()) {
        spdlog::info("  {}", route);
    }
    spdlog::info("");

    // ────────────────────────────────────────────────────────
    // Start Server
    // ────────────────────────────────────────────────────────

    HttpServer server(4);  // 4 worker threads
    g_server = &server;

    // Set router as the handler
    server.set_handler([&router](const HttpRequest& request) {
        return router.handle_request(request);
    });

    // Setup signal handler
    std::signal(SIGINT, signal_handler);

    // Listen
    auto listen_result = server.listen(port);
    if (listen_result.is_error()) {
        spdlog::error("Failed to start server: {}", listen_result.error());
        return 1;
    }

    spdlog::info("Server running on http://localhost:{}", port);
    spdlog::info("");
    spdlog::info("Try these commands:");
    spdlog::info("  curl http://localhost:{}/", port);
    spdlog::info("  curl http://localhost:{}/api/health", port);
    spdlog::info("  curl http://localhost:{}/api/users/123", port);
    spdlog::info("  curl -X POST http://localhost:{}/api/sync/start -d '{{\"file\":\"test.txt\"}}'", port);
    spdlog::info("");
    spdlog::info("Press Ctrl+C to stop");
    spdlog::info("");

    // Serve requests
    auto serve_result = server.serve_forever();
    if (serve_result.is_error()) {
        spdlog::error("Server error: {}", serve_result.error());
        return 1;
    }

    spdlog::info("Server shut down cleanly");
    return 0;
}
