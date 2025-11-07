#pragma once

#include "dfs/metadata/store.hpp"
#include "dfs/sync/merkle_tree.hpp"
#include "dfs/sync/session.hpp"
#include "dfs/sync/transfer.hpp"
#include "dfs/events/event_bus.hpp"
#include "dfs/events/events.hpp"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <vector>
#include <chrono>

namespace dfs::sync {

class SyncService {
public:
    SyncService(std::filesystem::path data_root,
                std::filesystem::path staging_root,
                events::EventBus& bus,
                metadata::MetadataStore& store);

    std::string register_client(const std::string& preferred_id = {});

    dfs::Result<SyncSessionInfo> start_session(const std::string& client_id);

    dfs::Result<DiffResponse> compute_diff(const std::string& session_id,
                                           const std::vector<metadata::FileMetadata>& client_snapshot);

    dfs::Result<void> ingest_chunk(const ChunkEnvelope& chunk);

    dfs::Result<metadata::FileMetadata> finalize_upload(const std::string& session_id,
                                                        const std::string& file_path,
                                                        const std::string& expected_hash);

    dfs::Result<std::string> read_file_hex(const std::string& file_path) const;

    dfs::Result<SyncSessionInfo> session_info(const std::string& session_id) const;

    metadata::MetadataStore& store() noexcept { return store_; }

private:
    struct SessionData {
        SyncSession session;
        std::unordered_set<std::string> pending_uploads;
        std::unordered_set<std::string> started_uploads;
        std::size_t total_upload_bytes = 0;
        std::size_t uploaded_bytes = 0;
        std::chrono::steady_clock::time_point started_at{std::chrono::steady_clock::now()};
    };

    metadata::MetadataStore& store_;
    events::EventBus& event_bus_;
    FileTransferService transfer_service_;

    std::filesystem::path data_root_;
    std::filesystem::path staging_root_;

    std::atomic<uint64_t> client_counter_{0};
    std::atomic<uint64_t> session_counter_{0};

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::string> clients_;
    std::unordered_map<std::string, SessionData> sessions_;

    metadata::FileMetadata build_metadata_from_disk(const std::string& client_id,
                                                    const std::string& file_path) const;

    dfs::Result<SessionData*> find_session(const std::string& session_id);
    dfs::Result<const SessionData*> find_session(const std::string& session_id) const;

    static std::unordered_map<std::string, metadata::FileMetadata>
    make_snapshot_map(const std::vector<metadata::FileMetadata>& snapshot);
};

} // namespace dfs::sync
