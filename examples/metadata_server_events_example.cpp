/**
 * @file metadata_server_events_example.cpp
 * @brief Phase 3: Event-Driven Metadata Server
 *
 * WHAT CHANGED FROM PHASE 2:
 * - Added EventBus for decoupled communication
 * - Handlers emit events instead of directly logging
 * - Components subscribe to events independently
 * - Clean separation of concerns
 *
 * BENEFITS:
 * - Add new features without modifying handlers
 * - Components are independent and testable
 * - Follows Observer pattern
 */

#include "dfs/network/http_server.hpp"
#include "dfs/network/http_router.hpp"
#include "dfs/metadata/types.hpp"
#include "dfs/metadata/parser.hpp"
#include "dfs/metadata/serializer.hpp"
#include "dfs/metadata/store.hpp"
#include "dfs/events/event_bus.hpp"
#include "dfs/events/events.hpp"
#include "dfs/events/components.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <csignal>

using namespace dfs::network;
using namespace dfs::metadata;
using namespace dfs::events;
using json = nlohmann::json;

// ════════════════════════════════════════════════════════════
// Global Objects
// ════════════════════════════════════════════════════════════

MetadataStore g_metadata_store;
EventBus g_event_bus;  // NEW: Global event bus
HttpServer* g_server = nullptr;

void signal_handler(int signal) {
    if (signal == SIGINT) {
        // Emit shutdown event
        g_event_bus.emit(ServerShuttingDownEvent{"SIGINT"});
        
        if (g_server) {
            g_server->stop();
        }
    }
}

// ════════════════════════════════════════════════════════════
// HTTP Handlers (Event-Driven)
// ════════════════════════════════════════════════════════════

HttpResponse handle_add_metadata(const HttpContext& ctx) {
    std::string ddl = ctx.request.body_as_string();

    // Parse DDL
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

    // Add to store
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

    // ════════════════════════════════════════════════════════════
    // NEW: Emit event instead of direct logging!
    // Components will react automatically
    // ════════════════════════════════════════════════════════════
    g_event_bus.emit(FileAddedEvent{metadata, "http"});

    // Return success
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

    // Serialize to binary
    std::vector<uint8_t> binary = Serializer::serialize(metadata);

    HttpResponse response(HttpStatus::OK);
    response.set_body(binary);
    response.set_header("Content-Type", "application/octet-stream");
    response.set_header("X-File-Path", metadata.file_path);
    response.set_header("X-File-Hash", metadata.hash);
    return response;
}

HttpResponse handle_list_metadata(const HttpContext& ctx) {
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

    FileMetadata new_metadata = parse_result.value();

    // Get old metadata for comparison
    auto old_result = g_metadata_store.get(new_metadata.file_path);
    
    // Update in store
    g_metadata_store.add_or_update(new_metadata);

    // Emit modified event if it existed before
    if (old_result.is_ok()) {
        auto old_metadata = old_result.value();
        g_event_bus.emit(FileModifiedEvent{
            new_metadata.file_path,
            old_metadata.hash,
            new_metadata.hash,
            old_metadata.size,
            new_metadata.size,
            "http"
        });
    } else {
        // Was actually an add
        g_event_bus.emit(FileAddedEvent{new_metadata, "http"});
    }

    json success_json = {
        {"status", "updated"},
        {"file_path", new_metadata.file_path},
        {"hash", new_metadata.hash},
        {"size", new_metadata.size}
    };

    HttpResponse response(HttpStatus::OK);
    response.set_body(success_json.dump(2));
    response.set_header("Content-Type", "application/json");
    return response;
}

HttpResponse handle_delete_metadata(const HttpContext& ctx) {
    std::string file_path = "/" + ctx.get_param("path");

    // Get metadata before deletion (for event)
    auto get_result = g_metadata_store.get(file_path);

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

    // Emit deleted event
    if (get_result.is_ok()) {
        g_event_bus.emit(FileDeletedEvent{file_path, get_result.value(), "http"});
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
    <title>DFS Metadata Server - Phase 3 (Event-Driven)</title>
    <style>
        body { font-family: Arial; max-width: 1000px; margin: 50px auto; }
        h1 { color: #333; }
        .event-badge { background: #28a745; color: white; padding: 4px 8px; border-radius: 4px; font-size: 12px; }
        .endpoint { background: #f4f4f4; padding: 15px; margin: 15px 0; border-left: 4px solid #0066cc; }
        code { background: #eee; padding: 2px 6px; border-radius: 3px; }
        pre { background: #282c34; color: #abb2bf; padding: 15px; border-radius: 5px; overflow-x: auto; }
    </style>
</head>
<body>
    <h1>🎯 DFS Metadata Server - Phase 3 <span class="event-badge">EVENT-DRIVEN</span></h1>
    <p><strong>Status:</strong> Running with EventBus</p>
    <p>This server demonstrates event-driven architecture with decoupled components.</p>

    <h2>What's New in Phase 3:</h2>
    <ul>
        <li><strong>EventBus:</strong> Type-safe event dispatch system</li>
        <li><strong>Components:</strong> Logger, Metrics, Sync (all decoupled!)</li>
        <li><strong>Observability:</strong> Automatic logging and metrics tracking</li>
        <li><strong>Extensible:</strong> Add new features without modifying handlers</li>
    </ul>

    <h2>Available Endpoints:</h2>

    <div class="endpoint">
        <strong>POST /metadata/add</strong><br>
        Add new file metadata (emits FileAddedEvent)<br><br>
        <strong>Example:</strong>
        <pre>curl -X POST http://localhost:8080/metadata/add \
  -d 'FILE "/test.txt" HASH "abc123" SIZE 1024 MODIFIED 1704096000 STATE SYNCED'</pre>
    </div>

    <div class="endpoint">
        <strong>GET /metadata/get/:path</strong><br>
        Get metadata for specific file (binary response)<br><br>
        <strong>Example:</strong>
        <pre>curl http://localhost:8080/metadata/get/test.txt > metadata.bin</pre>
    </div>

    <div class="endpoint">
        <strong>GET /metadata/list</strong><br>
        List all metadata (JSON)<br><br>
        <strong>Example:</strong>
        <pre>curl http://localhost:8080/metadata/list</pre>
    </div>

    <div class="endpoint">
        <strong>PUT /metadata/update</strong><br>
        Update existing metadata (emits FileModifiedEvent)<br><br>
        <strong>Example:</strong>
        <pre>curl -X PUT http://localhost:8080/metadata/update \
  -d 'FILE "/test.txt" HASH "new_hash" SIZE 2048 MODIFIED 1704096100 STATE SYNCED'</pre>
    </div>

    <div class="endpoint">
        <strong>DELETE /metadata/delete/:path</strong><br>
        Delete metadata (emits FileDeletedEvent)<br><br>
        <strong>Example:</strong>
        <pre>curl -X DELETE http://localhost:8080/metadata/delete/test.txt</pre>
    </div>

    <h2>Event Flow:</h2>
    <pre>
HTTP Handler → EventBus.emit(Event) → Components
                                      ├─ LoggerComponent (logs event)
                                      ├─ MetricsComponent (tracks stats)
                                      └─ SyncComponent (queues for sync)
    </pre>

    <hr>
    <p><em>Phase 3 - Event-Driven Architecture Complete ✅</em></p>
</body>
</html>
)";

    response.set_body(html);
    response.set_header("Content-Type", "text/html; charset=utf-8");
    return response;
}

// ════════════════════════════════════════════════════════════
// Main
// ════════════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    // Configure logging
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");

    // Parse port
    uint16_t port = 8080;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }

    spdlog::info("════════════════════════════════════════════");
    spdlog::info("DFS Metadata Server - Phase 3 (Event-Driven)");
    spdlog::info("════════════════════════════════════════════");
    spdlog::info("");

    // ════════════════════════════════════════════════════════════
    // NEW: Setup Event-Driven Components
    // ════════════════════════════════════════════════════════════

    LoggerComponent logger(g_event_bus);
    MetricsComponent metrics(g_event_bus);
    SyncComponent sync_manager(g_event_bus);

    spdlog::info("Event-driven components initialized:");
    spdlog::info("  - LoggerComponent (logs all file events)");
    spdlog::info("  - MetricsComponent (tracks statistics)");
    spdlog::info("  - SyncComponent (queues files for sync)");
    spdlog::info("");

    // ════════════════════════════════════════════════════════════
    // Setup Router
    // ════════════════════════════════════════════════════════════

    HttpRouter router;

    // Logging middleware
    router.use([](const HttpContext& ctx, HttpResponse& response) {
        spdlog::debug("{} {} from {}",
            HttpMethodUtils::to_string(ctx.request.method),
            ctx.request.url,
            ctx.request.get_header("User-Agent"));
        return true;
    });

    // Routes
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

    // ════════════════════════════════════════════════════════════
    // Start Server
    // ════════════════════════════════════════════════════════════

    HttpServer server(4);
    g_server = &server;

    server.set_handler([&router](const HttpRequest& request) {
        return router.handle_request(request);
    });

    std::signal(SIGINT, signal_handler);

    auto listen_result = server.listen(port);
    if (listen_result.is_error()) {
        spdlog::error("Failed to start server: {}", listen_result.error());
        return 1;
    }

    // Emit server started event
    g_event_bus.emit(ServerStartedEvent{port});

    spdlog::info("Try these commands:");
    spdlog::info("  curl http://localhost:{}/", port);
    spdlog::info("  curl -X POST http://localhost:{}/metadata/add -d 'FILE \"/test.txt\" HASH \"abc\" SIZE 100 STATE SYNCED'", port);
    spdlog::info("  curl http://localhost:{}/metadata/list", port);
    spdlog::info("");
    spdlog::info("Press Ctrl+C to stop");
    spdlog::info("");

    // Serve requests
    auto serve_result = server.serve_forever();
    if (serve_result.is_error()) {
        spdlog::error("Server error: {}", serve_result.error());
        return 1;
    }

    // ════════════════════════════════════════════════════════════
    // Shutdown - Print Metrics
    // ════════════════════════════════════════════════════════════

    metrics.print_stats();

    spdlog::info("Server shut down cleanly");
    return 0;
}
