#include "dfs/events/components.hpp"
#include "dfs/events/event_bus.hpp"
#include "dfs/events/events.hpp"
#include "dfs/metadata/store.hpp"
#include "dfs/network/http_router.hpp"
#include "dfs/network/http_server.hpp"
#include "dfs/sync/service.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using dfs::network::HttpContext;
using dfs::network::HttpResponse;
using dfs::network::HttpRouter;
using dfs::network::HttpServer;
using dfs::network::HttpStatus;
using dfs::network::HttpMethodUtils;

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

HttpResponse make_json_response(HttpStatus status, const json& body) {
    HttpResponse response(status);
    response.set_header("Content-Type", "application/json");
    response.set_body(body.dump(2));
    return response;
}

HttpResponse make_error(HttpStatus status, const std::string& message) {
    return make_json_response(status, json{{"error", message}});
}

json metadata_to_json(const dfs::metadata::FileMetadata& metadata) {
    json j;
    j["file_path"] = metadata.file_path;
    j["hash"] = metadata.hash;
    j["size"] = metadata.size;
    j["modified_time"] = metadata.modified_time;
    j["created_time"] = metadata.created_time;
    j["sync_state"] = static_cast<int>(metadata.sync_state);
    return j;
}

std::vector<dfs::metadata::FileMetadata> metadata_list_from_json(const json& arr) {
    std::vector<dfs::metadata::FileMetadata> items;
    if (!arr.is_array()) {
        return items;
    }
    for (const auto& entry : arr) {
        dfs::metadata::FileMetadata metadata;
        metadata.file_path = entry.value("file_path", "");
        metadata.hash = entry.value("hash", "");
        metadata.size = entry.value("size", 0);
        metadata.modified_time = entry.value("modified_time", static_cast<std::time_t>(0));
        metadata.created_time = entry.value("created_time", static_cast<std::time_t>(0));
        metadata.sync_state = static_cast<dfs::metadata::SyncState>(entry.value("sync_state", 0));
        items.push_back(metadata);
    }
    return items;
}

json session_info_to_json(const dfs::sync::SyncSessionInfo& info) {
    json j;
    j["session_id"] = info.session_id;
    j["client_id"] = info.client_id;
    j["files_pending"] = info.files_pending;
    j["bytes_pending"] = info.bytes_pending;
    j["state"] = static_cast<int>(info.state);
    j["last_error"] = info.last_error;
    return j;
}

std::vector<std::uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<std::uint8_t> out;
    if (hex.size() % 2 != 0) {
        return out;
    }
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        auto chunk = hex.substr(i, 2);
        try {
            std::uint8_t value = static_cast<std::uint8_t>(std::stoi(chunk, nullptr, 16));
            out.push_back(value);
        } catch (...) {
            out.clear();
            return out;
        }
    }
    return out;
}

std::string fnv1a_hex(const std::vector<std::uint8_t>& data) {
    const std::uint64_t offset = 0xcbf29ce484222325ULL;
    const std::uint64_t prime  = 0x100000001b3ULL;
    std::uint64_t hash = offset;
    for (auto byte : data) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= prime;
    }
    std::ostringstream oss;
    oss << std::hex << std::setw(sizeof(hash) * 2) << std::setfill('0') << hash;
    return oss.str();
}

std::string fnv1a_hex(const std::string& text) {
    return fnv1a_hex(std::vector<std::uint8_t>(text.begin(), text.end()));
}

} // namespace

int main(int argc, char* argv[]) {
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");

    uint16_t port = 8080;
    fs::path data_root = fs::current_path() / "sync_data";
    fs::path staging_root = data_root / "staging";
    fs::path files_root = data_root / "files";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if ((arg == "-d" || arg == "--data") && i + 1 < argc) {
            files_root = fs::path(argv[++i]);
        }
    }

    fs::create_directories(files_root);
    fs::create_directories(staging_root);

    dfs::events::EventBus event_bus;
    dfs::metadata::MetadataStore metadata_store;

    dfs::events::LoggerComponent logger(event_bus);
    dfs::events::MetricsComponent metrics(event_bus);
    dfs::events::SyncComponent sync_component(event_bus);

    dfs::sync::SyncService service(files_root, staging_root, event_bus, metadata_store);

    HttpRouter router;
    router.use([](const HttpContext& ctx, HttpResponse&) {
        spdlog::info("{} {}", HttpMethodUtils::to_string(ctx.request.method), ctx.request.url);
        return true;
    });

    router.post("/api/register", [&](const HttpContext& ctx) {
        auto payload = json::parse(ctx.request.body_as_string(), nullptr, false);
        if (payload.is_discarded()) {
            return make_error(HttpStatus::BAD_REQUEST, "Invalid JSON");
        }
        std::string preferred = payload.value("preferred_id", "");
        auto client_id = service.register_client(preferred);
        return make_json_response(HttpStatus::OK, json{{"client_id", client_id}});
    });

    router.post("/api/sync/start", [&](const HttpContext& ctx) {
        auto payload = json::parse(ctx.request.body_as_string(), nullptr, false);
        if (payload.is_discarded()) {
            return make_error(HttpStatus::BAD_REQUEST, "Invalid JSON");
        }
        std::string client_id = payload.value("client_id", "");
        if (client_id.empty()) {
            return make_error(HttpStatus::BAD_REQUEST, "client_id required");
        }
        auto result = service.start_session(client_id);
        if (result.is_error()) {
            return make_error(HttpStatus::BAD_REQUEST, result.error());
        }
        auto snapshot = metadata_store.list_all();
        json response;
        response["session"] = session_info_to_json(result.value());
        response["server_snapshot"] = json::array();
        for (const auto& item : snapshot) {
            response["server_snapshot"].push_back(metadata_to_json(item));
        }
        return make_json_response(HttpStatus::OK, response);
    });

    router.post("/api/sync/diff", [&](const HttpContext& ctx) {
        auto payload = json::parse(ctx.request.body_as_string(), nullptr, false);
        if (payload.is_discarded()) {
            return make_error(HttpStatus::BAD_REQUEST, "Invalid JSON");
        }
        std::string session_id = payload.value("session_id", "");
        if (session_id.empty()) {
            return make_error(HttpStatus::BAD_REQUEST, "session_id required");
        }
        auto snapshot = metadata_list_from_json(payload.value("snapshot", json::array()));
        auto diff = service.compute_diff(session_id, snapshot);
        if (diff.is_error()) {
            return make_error(HttpStatus::BAD_REQUEST, diff.error());
        }
        json response;
        response["files_to_upload"] = diff.value().files_to_upload;
        response["files_to_download"] = diff.value().files_to_download;
        response["files_to_delete_remote"] = diff.value().files_to_delete_remote;
        return make_json_response(HttpStatus::OK, response);
    });

    router.post("/api/file/upload_chunk", [&](const HttpContext& ctx) {
        auto payload = json::parse(ctx.request.body_as_string(), nullptr, false);
        if (payload.is_discarded()) {
            return make_error(HttpStatus::BAD_REQUEST, "Invalid JSON");
        }
        dfs::sync::ChunkEnvelope chunk;
        chunk.session_id = payload.value("session_id", "");
        chunk.file_path = payload.value("file_path", "");
        chunk.chunk_index = payload.value("chunk_index", 0);
        chunk.total_chunks = payload.value("total_chunks", 0);
        chunk.chunk_size = payload.value("chunk_size", 0);
        auto data_hex = payload.value("data", std::string{});
        chunk.data = hex_to_bytes(data_hex);
        chunk.chunk_hash = payload.value("chunk_hash", std::string{});
        if (chunk.data.empty() && !data_hex.empty()) {
            return make_error(HttpStatus::BAD_REQUEST, "Invalid chunk data");
        }
        auto result = service.ingest_chunk(chunk);
        if (result.is_error()) {
            return make_error(HttpStatus::BAD_REQUEST, result.error());
        }
        return make_json_response(HttpStatus::OK, json{{"status", "chunk_received"}});
    });

    router.post("/api/file/upload_complete", [&](const HttpContext& ctx) {
        auto payload = json::parse(ctx.request.body_as_string(), nullptr, false);
        if (payload.is_discarded()) {
            return make_error(HttpStatus::BAD_REQUEST, "Invalid JSON");
        }
        std::string session_id = payload.value("session_id", "");
        std::string file_path = payload.value("file_path", "");
        std::string expected_hash = payload.value("expected_hash", "");
        if (session_id.empty() || file_path.empty()) {
            return make_error(HttpStatus::BAD_REQUEST, "session_id and file_path required");
        }
        auto finalize = service.finalize_upload(session_id, file_path, expected_hash);
        if (finalize.is_error()) {
            return make_error(HttpStatus::BAD_REQUEST, finalize.error());
        }
        return make_json_response(HttpStatus::OK, metadata_to_json(finalize.value()));
    });

    router.post("/api/file/download", [&](const HttpContext& ctx) {
        auto payload = json::parse(ctx.request.body_as_string(), nullptr, false);
        if (payload.is_discarded()) {
            return make_error(HttpStatus::BAD_REQUEST, "Invalid JSON");
        }
        std::string file_path = payload.value("file_path", "");
        if (file_path.empty()) {
            return make_error(HttpStatus::BAD_REQUEST, "file_path required");
        }
        auto data_hex = service.read_file_hex(file_path);
        if (data_hex.is_error()) {
            return make_error(HttpStatus::NOT_FOUND, data_hex.error());
        }
        auto decoded = hex_to_bytes(data_hex.value());
        const std::string hash = fnv1a_hex(decoded);
        dfs::events::FileDownloadCompletedEvent evt{"manual", file_path, decoded.size()};
        event_bus.emit(evt);

        return make_json_response(HttpStatus::OK, json{{"data", data_hex.value()}, {"hash", hash}});
    });

    router.get("/api/sync/status", [&](const HttpContext& ctx) {
        auto session_id = ctx.get_param("session_id", "");
        if (session_id.empty()) {
            return make_error(HttpStatus::BAD_REQUEST, "session_id required");
        }
        auto info = service.session_info(session_id);
        if (info.is_error()) {
            return make_error(HttpStatus::BAD_REQUEST, info.error());
        }
        return make_json_response(HttpStatus::OK, session_info_to_json(info.value()));
    });

    router.post("/api/sync/status", [&](const HttpContext& ctx) {
        auto payload = json::parse(ctx.request.body_as_string(), nullptr, false);
        if (payload.is_discarded()) {
            return make_error(HttpStatus::BAD_REQUEST, "Invalid JSON");
        }
        std::string session_id = payload.value("session_id", "");
        if (session_id.empty()) {
            return make_error(HttpStatus::BAD_REQUEST, "session_id required");
        }
        auto info = service.session_info(session_id);
        if (info.is_error()) {
            return make_error(HttpStatus::BAD_REQUEST, info.error());
        }
        return make_json_response(HttpStatus::OK, session_info_to_json(info.value()));
    });

    HttpServer server(4);
    server.set_handler([&router](const dfs::network::HttpRequest& request) {
        return router.handle_request(request);
    });

    auto listen_result = server.listen(port);
    if (listen_result.is_error()) {
        spdlog::error("Failed to listen on port {}: {}", port, listen_result.error());
        return 1;
    }

    spdlog::info("Sync demo server listening on port {}", port);
    auto serve_result = server.serve_forever();
    if (serve_result.is_error()) {
        spdlog::error("Server error: {}", serve_result.error());
        return 1;
    }
    return 0;
}
