#pragma once

#include "dfs/metadata/types.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dfs::sync {

using FileMetadata = metadata::FileMetadata;

enum class SessionState {
    Idle,
    ComputingDiff,
    RequestingMetadata,
    TransferringFiles,
    ResolvingConflicts,
    ApplyingChanges,
    Complete,
    Failed
};

/**
 * @brief High-level summary of an active or completed sync session
 */
struct SyncSessionInfo {
    std::string session_id;
    std::string client_id;
    std::chrono::system_clock::time_point started_at{};
    SessionState state = SessionState::Idle;
    std::size_t files_pending = 0;
    std::size_t bytes_pending = 0;
    std::string last_error; ///< Populated when state == Failed
};

/**
 * @brief Snapshot point-in-time metadata used to compute diffs
 */
struct SnapshotEntry {
    std::string file_path;
    FileMetadata metadata;
};

/**
 * @brief Client → server payload describing local view before syncing
 */
struct DiffRequest {
    std::string session_id;
    std::vector<SnapshotEntry> local_snapshot;
};

/**
 * @brief Server reply instructing client which actions to take
 */
struct DiffResponse {
    std::vector<std::string> files_to_upload;
    std::vector<std::string> files_to_download;
    std::vector<std::string> files_to_delete_remote;
};

/**
 * @brief Payload for uploading/downloading a file chunk
 */
struct ChunkEnvelope {
    std::string session_id;
    std::string file_path;
    std::uint32_t chunk_index = 0;
    std::uint32_t total_chunks = 0;
    std::uint32_t chunk_size = 0;
    std::vector<std::uint8_t> data;
    std::string chunk_hash;   ///< Optional integrity checksum
};

/**
 * @brief Acknowledgement sent after server persists a chunk
 */
struct ChunkAck {
    std::string session_id;
    std::string file_path;
    std::uint32_t chunk_index = 0;
    bool accepted = true;
    std::string error_message;
};

} // namespace dfs::sync
