/**
 * @file metadata_server_asio_example.cpp
 * @brief Phase 2 Metadata Server using Boost.Asio (Event-Driven)
 *
 * WHY THIS VERSION:
 * This is the same metadata server as metadata_server_example.cpp,
 * but using the Boost.Asio event-driven HTTP server instead of the
 * thread pool server.
 *
 * DIFFERENCES FROM THREAD POOL VERSION:
 * - Uses Boost.Asio's io_context event loop
 * - Async I/O instead of blocking I/O
 * - Can handle 10,000+ concurrent connections
 * - Single-threaded event loop (can run multi-threaded too)
 *
 * HOW IT WORKS:
 * 1. io_context.run() starts the event loop
 * 2. HttpServerAsio accepts connections asynchronously
 * 3. Each connection reads/writes asynchronously
 * 4. Handler functions are called when requests arrive
 * 5. MetadataStore is still thread-safe (even though single-threaded now)
 *
 * Run with:
 *   ./build/examples/metadata_server_asio_example
 *
 * Test with:
 *   curl -X POST http://localhost:8080/metadata/add -d 'FILE "/test.txt" HASH "abc" SIZE 100 STATE SYNCED'
 *   curl http://localhost:8080/metadata/list
 */

#include "dfs/network/http_server_asio.hpp"  // Boost.Asio server
#include "dfs/network/http_router.hpp"
#include "dfs/metadata/types.hpp"
#include "dfs/metadata/parser.hpp"
#include "dfs/metadata/serializer.hpp"
#include "dfs/metadata/store.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <csignal>

using namespace dfs::network;
using namespace dfs::metadata;
using json = nlohmann::json;

// Global metadata store (shared across all connections)
// Thread-safe: MetadataStore has internal locking
MetadataStore g_metadata_store;

// Global io_context for signal handling
boost::asio::io_context* g_io_context = nullptr;

void signal_handler(int signal) {
    if (signal == SIGINT) {
        spdlog::info("Received SIGINT, shutting down...");
        if (g_io_context) {
            g_io_context->stop();
        }
    }
}

// ════════════════════════════════════════════════════════════
// HTTP Handlers (same as thread pool version)
// ════════════════════════════════════════════════════════════

HttpResponse handle_add_metadata(const HttpContext& ctx) {
    std::string ddl = ctx.request.body_as_string();
    spdlog::info("Adding metadata: {}", ddl);

    Parser parser(ddl);
    auto parse_result = parser.parse_file_metadata();

    if (parse_result.is_error()) {
        json error_json = {
            {"error", "parse_error"},
            {"message", parse_result.error()}
        };

        HttpResponse response(HttpStatus::BAD_REQUEST);
        response.set_body(error_json.dump(2));
        response.set_header("Content-Type", "application/json");
        return response;
    }

    FileMetadata metadata = parse_result.value();
    auto add_result = g_metadata_store.add(metadata);

    if (add_result.is_error()) {
        json error_json = {
            {"error", "already_exists"},
            {"message", add_result.error()},
            {"file_path", metadata.file_path}
        };

        HttpResponse response(HttpStatus::BAD_REQUEST);
        response.set_body(error_json.dump(2));
        response.set_header("Content-Type", "application/json");
        return response;
    }

    json success_json = {
        {"status", "added"},
        {"file_path", metadata.file_path},
        {"hash", metadata.hash},
        {"size", metadata.size}
    };

    HttpResponse response(HttpStatus::CREATED);
    response.set_body(success_json.dump(2));
    response.set_header("Content-Type", "application/json");
    return response;
}

HttpResponse handle_get_metadata(const HttpContext& ctx) {
    std::string file_path = "/" + ctx.get_param("path");
    spdlog::info("Getting metadata for: {}", file_path);

    auto get_result = g_metadata_store.get(file_path);

    if (get_result.is_error()) {
        json error_json = {
            {"error", "not_found"},
            {"message", get_result.error()},
            {"file_path", file_path}
        };

        HttpResponse response(HttpStatus::NOT_FOUND);
        response.set_body(error_json.dump(2));
        response.set_header("Content-Type", "application/json");
        return response;
    }

    FileMetadata metadata = get_result.value();
    std::vector<uint8_t> binary = Serializer::serialize(metadata);

    HttpResponse response(HttpStatus::OK);
    response.set_body(binary);
    response.set_header("Content-Type", "application/octet-stream");
    response.set_header("X-File-Path", metadata.file_path);
    response.set_header("X-File-Hash", metadata.hash);
    return response;
}

HttpResponse handle_list_metadata(const HttpContext& ctx) {
    spdlog::info("Listing all metadata");

    auto all_metadata = g_metadata_store.list_all();
    json metadata_array = json::array();

    for (const auto& metadata : all_metadata) {
        json metadata_json = {
            {"file_path", metadata.file_path},
            {"hash", metadata.hash},
            {"size", metadata.size},
            {"modified_time", metadata.modified_time},
            {"created_time", metadata.created_time},
            {"sync_state", SyncStateUtils::to_string(metadata.sync_state)},
            {"replica_count", metadata.replicas.size()}
        };

        json replicas_array = json::array();
        for (const auto& replica : metadata.replicas) {
            replicas_array.push_back({
                {"replica_id", replica.replica_id},
                {"version", replica.version},
                {"modified_time", replica.modified_time}
            });
        }
        metadata_json["replicas"] = replicas_array;

        metadata_array.push_back(metadata_json);
    }

    HttpResponse response(HttpStatus::OK);
    response.set_body(metadata_array.dump(2));
    response.set_header("Content-Type", "application/json");
    return response;
}

HttpResponse handle_update_metadata(const HttpContext& ctx) {
    std::string ddl = ctx.request.body_as_string();
    spdlog::info("Updating metadata: {}", ddl);

    Parser parser(ddl);
    auto parse_result = parser.parse_file_metadata();

    if (parse_result.is_error()) {
        json error_json = {
            {"error", "parse_error"},
            {"message", parse_result.error()}
        };

        HttpResponse response(HttpStatus::BAD_REQUEST);
        response.set_body(error_json.dump(2));
        response.set_header("Content-Type", "application/json");
        return response;
    }

    FileMetadata metadata = parse_result.value();
    g_metadata_store.add_or_update(metadata);

    json success_json = {
        {"status", "updated"},
        {"file_path", metadata.file_path},
        {"hash", metadata.hash},
        {"size", metadata.size}
    };

    HttpResponse response(HttpStatus::OK);
    response.set_body(success_json.dump(2));
    response.set_header("Content-Type", "application/json");
    return response;
}

HttpResponse handle_delete_metadata(const HttpContext& ctx) {
    std::string file_path = "/" + ctx.get_param("path");
    spdlog::info("Deleting metadata for: {}", file_path);

    auto remove_result = g_metadata_store.remove(file_path);

    if (remove_result.is_error()) {
        json error_json = {
            {"error", "not_found"},
            {"message", remove_result.error()},
            {"file_path", file_path}
        };

        HttpResponse response(HttpStatus::NOT_FOUND);
        response.set_body(error_json.dump(2));
        response.set_header("Content-Type", "application/json");
        return response;
    }

    json success_json = {
        {"status", "deleted"},
        {"file_path", file_path}
    };

    HttpResponse response(HttpStatus::OK);
    response.set_body(success_json.dump(2));
    response.set_header("Content-Type", "application/json");
    return response;
}

HttpResponse serve_homepage(const HttpContext& ctx) {
    HttpResponse response(HttpStatus::OK);

    std::string html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>DFS Metadata Server - Boost.Asio</title>
    <style>
        body { font-family: Arial; max-width: 1000px; margin: 50px auto; }
        h1 { color: #333; }
        .endpoint { background: #f4f4f4; padding: 15px; margin: 15px 0; border-left: 4px solid #0066cc; }
        .asio { background: #e8f4f8; border-left-color: #00a8cc; }
        code { background: #eee; padding: 2px 6px; border-radius: 3px; }
        pre { background: #282c34; color: #abb2bf; padding: 15px; border-radius: 5px; overflow-x: auto; }
    </style>
</head>
<body>
    <h1>🚀 DFS Metadata Server - Boost.Asio (Event-Driven)</h1>
    <p><strong>Status:</strong> Running</p>
    <p><strong>Server Type:</strong> Boost.Asio - Async I/O, Event Loop</p>
    <p>This version uses the event-driven Boost.Asio server for high-concurrency scenarios (10,000+ connections).</p>

    <div class="endpoint asio">
        <strong>⚡ Performance Characteristics</strong><br>
        <ul>
            <li>Non-blocking async I/O</li>
            <li>Single-threaded event loop (can be multi-threaded)</li>
            <li>Handles thousands of concurrent connections efficiently</li>
            <li>Lower memory footprint than thread pool</li>
        </ul>
    </div>

    <h2>Available Endpoints:</h2>

    <div class="endpoint">
        <strong>POST /metadata/add</strong><br>
        Add new file metadata (DDL format)<br><br>
        <strong>Example:</strong>
        <pre>curl -X POST http://localhost:8080/metadata/add \
  -d 'FILE "/test.txt" HASH "abc123" SIZE 1024 MODIFIED 1704096000 STATE SYNCED'</pre>
    </div>

    <div class="endpoint">
        <strong>GET /metadata/get/:path</strong><br>
        Get metadata for specific file (returns binary)<br><br>
        <strong>Example:</strong>
        <pre>curl http://localhost:8080/metadata/get/test.txt > metadata.bin</pre>
    </div>

    <div class="endpoint">
        <strong>GET /metadata/list</strong><br>
        List all metadata (returns JSON)<br><br>
        <strong>Example:</strong>
        <pre>curl http://localhost:8080/metadata/list</pre>
    </div>

    <div class="endpoint">
        <strong>PUT /metadata/update</strong><br>
        Update existing metadata (DDL format)<br><br>
        <strong>Example:</strong>
        <pre>curl -X PUT http://localhost:8080/metadata/update \
  -d 'FILE "/test.txt" HASH "new_hash" SIZE 2048 MODIFIED 1704096100 STATE SYNCED'</pre>
    </div>

    <div class="endpoint">
        <strong>DELETE /metadata/delete/:path</strong><br>
        Delete metadata<br><br>
        <strong>Example:</strong>
        <pre>curl -X DELETE http://localhost:8080/metadata/delete/test.txt</pre>
    </div>

    <hr>
    <p><em>Phase 2 - Metadata System with Boost.Asio Event-Driven Server ✅</em></p>
</body>
</html>
)";

    response.set_body(html);
    response.set_header("Content-Type", "text/html; charset=utf-8");
    return response;
}

// ════════════════════════════════════════════════════════════
// Main: Setup Router and Boost.Asio Server
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
    spdlog::info("DFS Metadata Server - Boost.Asio");
    spdlog::info("════════════════════════════════════════════");
    spdlog::info("");

    // ────────────────────────────────────────────────────────
    // Setup Router (same as thread pool version)
    // ────────────────────────────────────────────────────────

    HttpRouter router;

    // Logging middleware
    router.use([](const HttpContext& ctx, HttpResponse& response) {
        spdlog::info("{} {} from {}",
            HttpMethodUtils::to_string(ctx.request.method),
            ctx.request.url,
            ctx.request.get_header("User-Agent"));
        return true;
    });

    // Register routes
    router.get("/", serve_homepage);
    router.post("/metadata/add", handle_add_metadata);
    router.get("/metadata/get/*", handle_get_metadata);
    router.get("/metadata/list", handle_list_metadata);
    router.put("/metadata/update", handle_update_metadata);
    router.delete_("/metadata/delete/*", handle_delete_metadata);

    spdlog::info("Registered routes:");
    for (const auto& route : router.list_routes()) {
        spdlog::info("  {}", route);
    }
    spdlog::info("");

    // ────────────────────────────────────────────────────────
    // Setup Boost.Asio Event Loop
    // ────────────────────────────────────────────────────────

    try {
        // Create io_context (event loop)
        boost::asio::io_context io_context;
        g_io_context = &io_context;

        // Create Asio-based HTTP server
        HttpServerAsio server(io_context, port);

        // Set router as handler
        server.set_handler([&router](const HttpRequest& request) {
            return router.handle_request(request);
        });

        // Setup signal handler
        std::signal(SIGINT, signal_handler);

        spdlog::info("Server running on http://localhost:{}", port);
        spdlog::info("Server type: Boost.Asio (Event-Driven, Async I/O)");
        spdlog::info("");
        spdlog::info("Try these commands:");
        spdlog::info("  curl http://localhost:{}/", port);
        spdlog::info("  curl -X POST http://localhost:{}/metadata/add -d 'FILE \"/test.txt\" HASH \"abc\" SIZE 100 STATE SYNCED'", port);
        spdlog::info("  curl http://localhost:{}/metadata/list", port);
        spdlog::info("");
        spdlog::info("Press Ctrl+C to stop");
        spdlog::info("");

        // Run event loop (blocks until stopped)
        // This is THE event loop - all async operations run here
        io_context.run();

        spdlog::info("Server shut down cleanly");
        return 0;

    } catch (const std::exception& e) {
        spdlog::error("Server error: {}", e.what());
        return 1;
    }
}
