/**
 * @file metadata_server_example.cpp
 * @brief Complete Phase 2 integration: Metadata + HTTP Server
 *
 * WHY THIS FILE EXISTS:
 * This demonstrates how all Phase 2 components work together:
 * 1. HTTP Server receives requests
 * 2. Router routes to appropriate handlers
 * 3. Handlers use Parser to parse DDL
 * 4. Store saves/retrieves metadata
 * 5. Serializer converts to binary for response
 *
 * THIS IS THE CORE OF PHASE 2 INTEGRATION!
 *
 * ENDPOINTS:
 * - POST /metadata/add     - Add new file metadata (DDL format)
 * - GET  /metadata/get/:path - Get metadata for specific file (binary response)
 * - GET  /metadata/list    - List all metadata (JSON array)
 * - PUT  /metadata/update  - Update existing metadata (DDL format)
 * - DELETE /metadata/delete/:path - Delete metadata
 *
 * EXAMPLE USAGE:
 * # Start server
 * ./metadata_server_example
 *
 * # Add metadata
 * curl -X POST http://localhost:8080/metadata/add \
 *   -d 'FILE "/test.txt" HASH "abc123" SIZE 1024 MODIFIED 1704096000 STATE SYNCED'
 *
 * # Get metadata (returns binary)
 * curl http://localhost:8080/metadata/get/test.txt > metadata.bin
 *
 * # List all (returns JSON)
 * curl http://localhost:8080/metadata/list
 *
 * HOW IT INTEGRATES:
 * Client App ←→ HTTP ←→ Router ←→ Handlers ←→ Store
 *                                      ↓
 *                                   Parser
 *                                   Serializer
 */

#include "dfs/network/http_server.hpp"
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

// Global metadata store (shared across all HTTP handlers)
// WHY GLOBAL: All HTTP worker threads need access to same store
// Thread-safe: MetadataStore has internal locking
MetadataStore g_metadata_store;

// Global server for signal handling
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
// HTTP Handlers (Integration of Metadata + HTTP)
// ════════════════════════════════════════════════════════════

/**
 * POST /metadata/add
 * Add new file metadata
 *
 * WHY THIS ENDPOINT:
 * When client uploads a new file, it sends metadata to server.
 *
 * FLOW:
 * 1. Receive DDL in request body
 * 2. Parse DDL → FileMetadata
 * 3. Add to store
 * 4. Return success/error
 *
 * EXAMPLE:
 * POST /metadata/add
 * Body: FILE "/test.txt" HASH "abc123" SIZE 1024 STATE SYNCED
 */
HttpResponse handle_add_metadata(const HttpContext& ctx) {
    // Get DDL from request body
    std::string ddl = ctx.request.body_as_string();

    spdlog::info("Adding metadata: {}", ddl);

    // Parse DDL → FileMetadata
    Parser parser(ddl);
    auto parse_result = parser.parse_file_metadata();

    if (parse_result.is_error()) {
        // Parse error - return 400 Bad Request
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
        // Already exists - return 409 Conflict
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

    // Success!
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

/**
 * GET /metadata/get/:path
 * Get metadata for specific file (returns binary)
 *
 * WHY THIS ENDPOINT:
 * Client needs to check if its local file matches server version.
 *
 * FLOW:
 * 1. Extract file path from URL
 * 2. Get from store
 * 3. Serialize to binary
 * 4. Return binary response
 *
 * EXAMPLE:
 * GET /metadata/get/test.txt
 * Response: <binary data>
 */
HttpResponse handle_get_metadata(const HttpContext& ctx) {
    // Extract file path from URL parameter
    std::string file_path = "/" + ctx.get_param("path");

    spdlog::info("Getting metadata for: {}", file_path);

    // Get from store
    auto get_result = g_metadata_store.get(file_path);

    if (get_result.is_error()) {
        // Not found - return 404
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

    // Return binary response
    HttpResponse response(HttpStatus::OK);
    response.set_body(binary);
    response.set_header("Content-Type", "application/octet-stream");
    response.set_header("X-File-Path", metadata.file_path);
    response.set_header("X-File-Hash", metadata.hash);
    return response;
}

/**
 * GET /metadata/list
 * List all metadata (returns JSON array)
 *
 * WHY THIS ENDPOINT:
 * Client synchronization: "Give me all files you have, I'll compare with mine"
 *
 * FLOW:
 * 1. Get all from store
 * 2. Convert to JSON array
 * 3. Return JSON
 *
 * EXAMPLE:
 * GET /metadata/list
 * Response:
 * [
 *   {"file_path": "/test1.txt", "hash": "abc", "size": 100, ...},
 *   {"file_path": "/test2.txt", "hash": "def", "size": 200, ...}
 * ]
 */
HttpResponse handle_list_metadata(const HttpContext& ctx) {
    spdlog::info("Listing all metadata");

    // Get all from store
    auto all_metadata = g_metadata_store.list_all();

    // Convert to JSON array
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

        // Add replicas
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

    // Return JSON
    HttpResponse response(HttpStatus::OK);
    response.set_body(metadata_array.dump(2));
    response.set_header("Content-Type", "application/json");
    return response;
}

/**
 * PUT /metadata/update
 * Update existing metadata
 *
 * WHY THIS ENDPOINT:
 * When client modifies a file, it updates metadata on server.
 *
 * FLOW:
 * 1. Receive DDL in request body
 * 2. Parse DDL → FileMetadata
 * 3. Update in store
 * 4. Return success/error
 *
 * EXAMPLE:
 * PUT /metadata/update
 * Body: FILE "/test.txt" HASH "new_hash" SIZE 2048 MODIFIED 1704096100 STATE SYNCED
 */
HttpResponse handle_update_metadata(const HttpContext& ctx) {
    // Get DDL from request body
    std::string ddl = ctx.request.body_as_string();

    spdlog::info("Updating metadata: {}", ddl);

    // Parse DDL → FileMetadata
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

    // Update in store (or add if doesn't exist)
    g_metadata_store.add_or_update(metadata);

    // Success!
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

/**
 * DELETE /metadata/delete/:path
 * Delete metadata
 *
 * WHY THIS ENDPOINT:
 * When client deletes a file, remove metadata from server.
 *
 * FLOW:
 * 1. Extract file path from URL
 * 2. Remove from store
 * 3. Return success/error
 *
 * EXAMPLE:
 * DELETE /metadata/delete/test.txt
 */
HttpResponse handle_delete_metadata(const HttpContext& ctx) {
    // Extract file path from URL parameter
    std::string file_path = "/" + ctx.get_param("path");

    spdlog::info("Deleting metadata for: {}", file_path);

    // Remove from store
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

    // Success!
    json success_json = {
        {"status", "deleted"},
        {"file_path", file_path}
    };

    HttpResponse response(HttpStatus::OK);
    response.set_body(success_json.dump(2));
    response.set_header("Content-Type", "application/json");
    return response;
}

/**
 * GET /
 * Homepage with documentation
 */
HttpResponse serve_homepage(const HttpContext& ctx) {
    HttpResponse response(HttpStatus::OK);

    std::string html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>DFS Metadata Server - Phase 2</title>
    <style>
        body { font-family: Arial; max-width: 1000px; margin: 50px auto; }
        h1 { color: #333; }
        .endpoint { background: #f4f4f4; padding: 15px; margin: 15px 0; border-left: 4px solid #0066cc; }
        code { background: #eee; padding: 2px 6px; border-radius: 3px; }
        pre { background: #282c34; color: #abb2bf; padding: 15px; border-radius: 5px; overflow-x: auto; }
    </style>
</head>
<body>
    <h1>🎯 DFS Metadata Server - Phase 2</h1>
    <p><strong>Status:</strong> Running</p>
    <p>This server implements the complete Phase 2 metadata system with HTTP integration.</p>

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

    <h2>Phase 2 Components:</h2>
    <ul>
        <li><strong>Parser:</strong> Converts DDL text to FileMetadata structs</li>
        <li><strong>Lexer:</strong> Tokenizes DDL input</li>
        <li><strong>Store:</strong> Thread-safe in-memory metadata storage</li>
        <li><strong>Serializer:</strong> Binary serialization for network efficiency</li>
        <li><strong>HTTP Router:</strong> Routes requests to handlers</li>
    </ul>

    <h2>Complete Workflow Example:</h2>
    <pre># 1. Add metadata
curl -X POST http://localhost:8080/metadata/add \
  -d 'FILE "/docs/project.txt" HASH "abc123def456" SIZE 5120 MODIFIED 1704096000 STATE SYNCED REPLICA "laptop_1" VERSION 1 MODIFIED 1704096000'

# 2. List all
curl http://localhost:8080/metadata/list | jq .

# 3. Get specific file
curl http://localhost:8080/metadata/get/docs/project.txt > metadata.bin

# 4. Update
curl -X PUT http://localhost:8080/metadata/update \
  -d 'FILE "/docs/project.txt" HASH "new_hash_after_edit" SIZE 6144 MODIFIED 1704096100 STATE SYNCED REPLICA "laptop_1" VERSION 2 MODIFIED 1704096100'

# 5. Delete
curl -X DELETE http://localhost:8080/metadata/delete/docs/project.txt</pre>

    <hr>
    <p><em>Phase 2 - Metadata & DDL System Complete ✅</em></p>
</body>
</html>
)";

    response.set_body(html);
    response.set_header("Content-Type", "text/html; charset=utf-8");
    return response;
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
    spdlog::info("DFS Metadata Server - Phase 2");
    spdlog::info("════════════════════════════════════════════");
    spdlog::info("");

    // ────────────────────────────────────────────────────────
    // Setup Router
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

    // ────────────────────────────────────────────────────────
    // Register Routes
    // ────────────────────────────────────────────────────────

    // Homepage
    router.get("/", serve_homepage);

    // Metadata API
    router.post("/metadata/add", handle_add_metadata);
    router.get("/metadata/get/*", handle_get_metadata);  // Wildcard for file paths
    router.get("/metadata/list", handle_list_metadata);
    router.put("/metadata/update", handle_update_metadata);
    router.delete_("/metadata/delete/*", handle_delete_metadata);

    // ────────────────────────────────────────────────────────
    // List routes
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

    // Set router as handler
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

    spdlog::info("Server shut down cleanly");
    return 0;
}
