#include "dfs/sync/service.hpp"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <optional>

namespace dfs::sync {
namespace fs = std::filesystem;
namespace {

std::string generate_id(const std::string& base, uint64_t counter) {
    std::ostringstream oss;
    if (base.empty()) {
        oss << "client-" << counter;
    } else {
        oss << base << "-" << counter;
    }
    return oss.str();
}

std::string hex_encode(const std::vector<std::uint8_t>& data) {
    std::ostringstream oss;
    for (auto byte : data) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return oss.str();
}

std::vector<std::uint8_t> hex_decode(const std::string& hex) {
    std::vector<std::uint8_t> bytes;
    if (hex.size() % 2 != 0) {
        return bytes;
    }
    bytes.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        auto part = hex.substr(i, 2);
        std::uint8_t value = static_cast<std::uint8_t>(std::stoi(part, nullptr, 16));
        bytes.push_back(value);
    }
    return bytes;
}

std::string compute_file_hash(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    const std::uint64_t offset = 0xcbf29ce484222325ULL;
    const std::uint64_t prime  = 0x100000001b3ULL;
    std::uint64_t hash = offset;
    char buffer[4096];
    while (input.read(buffer, sizeof(buffer)) || input.gcount() > 0) {
        const std::streamsize count = input.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(buffer[i]));
            hash *= prime;
        }
    }
    std::ostringstream hex;
    hex << std::hex << std::setw(sizeof(hash) * 2) << std::setfill('0') << hash;
    return hex.str();
}

std::optional<metadata::ReplicaInfo> find_replica(const metadata::FileMetadata& metadata,
                                                   const std::string& replica_id) {
    for (const auto& replica : metadata.replicas) {
        if (replica.replica_id == replica_id) {
            return replica;
        }
    }
    return std::nullopt;
}

} // namespace

SyncService::SyncService(fs::path data_root,
                         fs::path staging_root,
                         events::EventBus& bus,
                         metadata::MetadataStore& store)
    : store_(store),
      event_bus_(bus),
      data_root_(std::move(data_root)),
      staging_root_(std::move(staging_root)) {

    fs::create_directories(data_root_);
    fs::create_directories(staging_root_);
}

std::string SyncService::register_client(const std::string& preferred_id) {
    std::lock_guard lock(mutex_);
    const auto counter = ++client_counter_;
    std::string candidate = preferred_id;
    if (candidate.empty()) {
        candidate = generate_id("client", counter);
    }
    while (clients_.count(candidate) > 0) {
        candidate = generate_id(candidate, ++client_counter_);
    }
    clients_[candidate] = candidate;
    return candidate;
}

dfs::Result<SyncSessionInfo> SyncService::start_session(const std::string& client_id) {
    std::lock_guard lock(mutex_);
    if (clients_.find(client_id) == clients_.end()) {
        return dfs::Err<SyncSessionInfo>(std::string("Unknown client: ") + client_id);
    }
    const auto session_id = "session-" + std::to_string(++session_counter_);
    SessionData session_data{SyncSession(session_id, client_id)};
    auto result = session_data.session.start(0, 0);
    if (result.is_error()) {
        return dfs::Err<SyncSessionInfo>(result.error());
    }
    session_data.started_at = std::chrono::steady_clock::now();
    sessions_.emplace(session_id, std::move(session_data));

    event_bus_.emit(events::SyncStartedEvent{client_id, store_.size()});
    return dfs::Ok(sessions_.at(session_id).session.info());
}

dfs::Result<DiffResponse> SyncService::compute_diff(const std::string& session_id,
                                                     const std::vector<metadata::FileMetadata>& client_snapshot) {
    std::lock_guard lock(mutex_);
    auto session_result = find_session(session_id);
    if (session_result.is_error()) {
        return dfs::Err<DiffResponse>(session_result.error());
    }
    auto* session_data = session_result.value();

    if (session_data->session.state() == SessionState::Idle) {
        auto transition = session_data->session.transition_to(SessionState::RequestingMetadata);
        if (transition.is_error()) {
            return dfs::Err<DiffResponse>(transition.error());
        }
    }

    auto server_metadata = store_.list_all();
    MerkleTree client_tree;
    client_tree.build(client_snapshot);
    MerkleTree server_tree;
    server_tree.build(server_metadata);

    const auto differences = client_tree.diff(server_tree);
    const auto client_map = make_snapshot_map(client_snapshot);
    const auto server_map = make_snapshot_map(server_metadata);

    DiffResponse response;
    std::size_t total_upload_bytes = 0;

    for (const auto& path : differences) {
        const bool client_has = client_map.find(path) != client_map.end();
        const bool server_has = server_map.find(path) != server_map.end();

        if (client_has && (!server_has || client_map.at(path).hash != server_map.at(path).hash)) {
            response.files_to_upload.push_back(path);
            total_upload_bytes += client_map.at(path).size;
        } else if (!client_has && server_has) {
            response.files_to_download.push_back(path);
        }
    }

    // Handle server-only files not present in client's diff list
    for (const auto& [path, metadata] : server_map) {
        if (client_map.find(path) == client_map.end()) {
            response.files_to_download.push_back(path);
        }
    }

    session_data->pending_uploads = {response.files_to_upload.begin(), response.files_to_upload.end()};
    session_data->started_uploads.clear();
    session_data->total_upload_bytes = total_upload_bytes;
    session_data->uploaded_bytes = 0;

    session_data->session.update_pending(session_data->pending_uploads.size(), total_upload_bytes);
    session_data->session.transition_to(SessionState::TransferringFiles);

    return dfs::Ok(response);
}

dfs::Result<void> SyncService::ingest_chunk(const ChunkEnvelope& chunk) {
    std::lock_guard lock(mutex_);
    auto session_result = find_session(chunk.session_id);
    if (session_result.is_error()) {
        return dfs::Err<void>(session_result.error());
    }
    auto* session_data = session_result.value();

    if (session_data->pending_uploads.find(chunk.file_path) == session_data->pending_uploads.end()) {
        return dfs::Err<void>(std::string("File not scheduled for upload: ") + chunk.file_path);
    }

    if (session_data->started_uploads.insert(chunk.file_path).second) {
        events::FileUploadStartedEvent started{chunk.session_id, chunk.file_path,
                                              static_cast<std::size_t>(chunk.total_chunks) * chunk.chunk_size};
        event_bus_.emit(started);
    }

    auto result = transfer_service_.apply_chunk(chunk, staging_root_);
    if (result.is_error()) {
        session_data->session.mark_failed(result.error());
        event_bus_.emit(events::SyncFailedEvent{session_data->session.client_id(), result.error()});
        return result;
    }

    events::FileChunkReceivedEvent evt{chunk.session_id, chunk.file_path, chunk.chunk_index,
                                       chunk.total_chunks, chunk.data.size()};
    event_bus_.emit(evt);
    return dfs::Ok();
}

dfs::Result<metadata::FileMetadata> SyncService::finalize_upload(const std::string& session_id,
                                                                  const std::string& file_path,
                                                                  const std::string& expected_hash) {
    std::lock_guard lock(mutex_);
    auto session_result = find_session(session_id);
    if (session_result.is_error()) {
        return dfs::Err<metadata::FileMetadata>(session_result.error());
    }
    auto* session_data = session_result.value();

    auto finalize_result = transfer_service_.finalize_file(session_id, file_path, staging_root_, data_root_, expected_hash);
    if (finalize_result.is_error()) {
        session_data->session.mark_failed(finalize_result.error());
        event_bus_.emit(events::SyncFailedEvent{session_data->session.client_id(), finalize_result.error()});
        return dfs::Err<metadata::FileMetadata>(finalize_result.error());
    }

    auto new_metadata = build_metadata_from_disk(session_data->session.client_id(), file_path);
    if (new_metadata.hash != expected_hash) {
        session_data->session.mark_failed("Hash mismatch after finalize");
        event_bus_.emit(events::SyncFailedEvent{session_data->session.client_id(), "Hash mismatch after finalize"});
        return dfs::Err<metadata::FileMetadata>(std::string("Hash mismatch after finalize for ") + file_path);
    }

    auto previous = store_.get(file_path);
    if (previous.is_ok()) {
        new_metadata.replicas = previous.value().replicas;
    }
    uint32_t next_version = 1;
    if (previous.is_ok()) {
        if (auto replica = find_replica(previous.value(), session_data->session.client_id())) {
            next_version = replica->version + 1;
        }
    }
    new_metadata.update_replica(session_data->session.client_id(), next_version, new_metadata.modified_time);
    if (previous.is_ok()) {
        event_bus_.emit(events::FileModifiedEvent{file_path,
                                                  previous.value().hash,
                                                  new_metadata.hash,
                                                  previous.value().size,
                                                  new_metadata.size,
                                                  "sync"});
    } else {
        event_bus_.emit(events::FileAddedEvent{new_metadata, "sync"});
    }

    store_.add_or_update(new_metadata);

    const auto upload_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - session_data->started_at);
    events::FileUploadCompletedEvent completed{session_id, file_path, new_metadata.hash, new_metadata.size,
                                               upload_duration};
    event_bus_.emit(completed);

    session_data->pending_uploads.erase(file_path);
    session_data->uploaded_bytes += new_metadata.size;
    session_data->session.update_pending(session_data->pending_uploads.size(),
                                         session_data->total_upload_bytes - session_data->uploaded_bytes);

    if (session_data->pending_uploads.empty()) {
        session_data->session.transition_to(SessionState::ApplyingChanges);
        session_data->session.transition_to(SessionState::Complete);
        const auto sync_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - session_data->started_at);
        events::SyncCompletedEvent done{session_data->session.client_id(), store_.size(), sync_duration};
        event_bus_.emit(done);
    }

    return dfs::Ok(new_metadata);
}

dfs::Result<std::string> SyncService::read_file_hex(const std::string& file_path) const {
    fs::path absolute = data_root_ / fs::path(file_path).relative_path();
    if (!fs::exists(absolute)) {
        return dfs::Err<std::string>(std::string("File not found: ") + file_path);
    }

    std::ifstream input(absolute, std::ios::binary);
    if (!input) {
        return dfs::Err<std::string>(std::string("Failed to open file: ") + file_path);
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string data = buffer.str();
    std::vector<std::uint8_t> bytes(data.begin(), data.end());
    return dfs::Ok(hex_encode(bytes));
}

dfs::Result<SyncSessionInfo> SyncService::session_info(const std::string& session_id) const {
    std::lock_guard lock(mutex_);
    auto session_result = find_session(session_id);
    if (session_result.is_error()) {
        return dfs::Err<SyncSessionInfo>(session_result.error());
    }
    return dfs::Ok(session_result.value()->session.info());
}

metadata::FileMetadata SyncService::build_metadata_from_disk(const std::string& client_id,
                                                             const std::string& file_path) const {
    fs::path absolute = data_root_ / fs::path(file_path).relative_path();
    metadata::FileMetadata metadata;
    metadata.file_path = file_path;
    metadata.size = fs::exists(absolute) ? fs::file_size(absolute) : 0;
    metadata.hash = compute_file_hash(absolute);
    const auto now = std::time(nullptr);
    metadata.modified_time = now;
    metadata.created_time = now;
    metadata.sync_state = metadata::SyncState::SYNCED;
    return metadata;
}

dfs::Result<SyncService::SessionData*> SyncService::find_session(const std::string& session_id) {
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return dfs::Err<SessionData*>(std::string("Unknown session: ") + session_id);
    }
    return dfs::Ok(&it->second);
}

dfs::Result<const SyncService::SessionData*> SyncService::find_session(const std::string& session_id) const {
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return dfs::Err<const SessionData*>(std::string("Unknown session: ") + session_id);
    }
    return dfs::Ok(&it->second);
}

std::unordered_map<std::string, metadata::FileMetadata>
SyncService::make_snapshot_map(const std::vector<metadata::FileMetadata>& snapshot) {
    std::unordered_map<std::string, metadata::FileMetadata> map;
    for (const auto& entry : snapshot) {
        map.emplace(entry.file_path, entry);
    }
    return map;
}

} // namespace dfs::sync
