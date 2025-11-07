/**
 * @file events.hpp
 * @brief Event type definitions for file sync system
 *
 * WHY THIS FILE EXISTS:
 * Defines all event types used in the system.
 * Events are the messages passed between components.
 *
 * NAMING CONVENTION:
 * - Events are past-tense: FileAddedEvent, FileDeletedEvent
 * - Commands are imperative: AddFileCommand, DeleteFileCommand
 */

#pragma once

#include "dfs/metadata/types.hpp"
#include <chrono>
#include <cstdint>
#include <string>

namespace dfs::events {

// ════════════════════════════════════════════════════════
// File Events
// ════════════════════════════════════════════════════════

/**
 * @brief Emitted when a file's metadata is added to the system
 *
 * WHO EMITS:
 * - HTTP handler (POST /metadata/add)
 * - File watcher (detects new file)
 * - Sync manager (receives file from remote)
 *
 * WHO SUBSCRIBES:
 * - Logger (log the addition)
 * - Sync manager (queue for upload)
 * - Metrics (track file additions)
 * - Cache (invalidate stale data)
 */
struct FileAddedEvent {
    metadata::FileMetadata metadata;
    std::string source;  // "http", "watcher", "sync"
    std::chrono::system_clock::time_point timestamp;

    FileAddedEvent(
        metadata::FileMetadata m,
        std::string src = "unknown"
    ) : metadata(std::move(m)),
        source(std::move(src)),
        timestamp(std::chrono::system_clock::now())
    {}
};

/**
 * @brief Emitted when a file's metadata is modified
 *
 * WHO EMITS:
 * - HTTP handler (PUT /metadata/update)
 * - File watcher (detects file change)
 * - Sync manager (receives update from remote)
 *
 * WHO SUBSCRIBES:
 * - Logger (log the change)
 * - Sync manager (queue for re-upload)
 * - Metrics (track modifications)
 * - Cache (invalidate)
 */
struct FileModifiedEvent {
    std::string file_path;
    std::string old_hash;
    std::string new_hash;
    size_t old_size;
    size_t new_size;
    std::string source;
    std::chrono::system_clock::time_point timestamp;

    FileModifiedEvent(
        std::string path,
        std::string old_h,
        std::string new_h,
        size_t old_s,
        size_t new_s,
        std::string src = "unknown"
    ) : file_path(std::move(path)),
        old_hash(std::move(old_h)),
        new_hash(std::move(new_h)),
        old_size(old_s),
        new_size(new_s),
        source(std::move(src)),
        timestamp(std::chrono::system_clock::now())
    {}
};

/**
 * @brief Emitted when a file's metadata is deleted
 *
 * WHO EMITS:
 * - HTTP handler (DELETE /metadata/delete/:path)
 * - File watcher (detects file deletion)
 * - Sync manager (receives delete from remote)
 *
 * WHO SUBSCRIBES:
 * - Logger (log the deletion)
 * - Sync manager (propagate delete to remotes)
 * - Metrics (track deletions)
 * - Cache (invalidate)
 */
struct FileDeletedEvent {
    std::string file_path;
    metadata::FileMetadata last_metadata;  // For recovery/undo
    std::string source;
    std::chrono::system_clock::time_point timestamp;

    FileDeletedEvent(
        std::string path,
        metadata::FileMetadata last_meta,
        std::string src = "unknown"
    ) : file_path(std::move(path)),
        last_metadata(std::move(last_meta)),
        source(std::move(src)),
        timestamp(std::chrono::system_clock::now())
    {}
};

// ════════════════════════════════════════════════════════
// Server Events
// ════════════════════════════════════════════════════════

/**
 * @brief Emitted when server starts
 *
 * WHO EMITS: main() startup
 * WHO SUBSCRIBES: Any component that needs initialization
 */
struct ServerStartedEvent {
    uint16_t port;
    std::chrono::system_clock::time_point timestamp;

    explicit ServerStartedEvent(uint16_t p)
        : port(p),
          timestamp(std::chrono::system_clock::now())
    {}
};

/**
 * @brief Emitted when server is shutting down
 *
 * WHO EMITS: main() shutdown
 * WHO SUBSCRIBES: Components that need cleanup
 */
struct ServerShuttingDownEvent {
    std::string reason;
    std::chrono::system_clock::time_point timestamp;

    explicit ServerShuttingDownEvent(std::string r = "normal")
        : reason(std::move(r)),
          timestamp(std::chrono::system_clock::now())
    {}
};

// ════════════════════════════════════════════════════════
// Sync Events
// ════════════════════════════════════════════════════════

enum class ConflictResolutionStrategy {
    LastWriteWins,
    Manual,
    Merge
};

/**
 * @brief Emitted when sync session starts
 */
struct SyncStartedEvent {
    std::string node_id;
    size_t file_count;
    std::chrono::system_clock::time_point timestamp;

    SyncStartedEvent(std::string id, size_t count)
        : node_id(std::move(id)),
          file_count(count),
          timestamp(std::chrono::system_clock::now())
    {}
};

/**
 * @brief Emitted when sync session completes
 */
struct SyncCompletedEvent {
    std::string node_id;
    size_t files_synced;
    std::chrono::milliseconds duration;
    std::chrono::system_clock::time_point timestamp;

    SyncCompletedEvent(
        std::string id,
        size_t count,
        std::chrono::milliseconds dur
    ) : node_id(std::move(id)),
        files_synced(count),
        duration(dur),
        timestamp(std::chrono::system_clock::now())
    {}
};

/**
 * @brief Emitted when sync session fails
 */
struct SyncFailedEvent {
    std::string node_id;
    std::string error_message;
    std::chrono::system_clock::time_point timestamp;

    SyncFailedEvent(std::string id, std::string err)
        : node_id(std::move(id)),
          error_message(std::move(err)),
          timestamp(std::chrono::system_clock::now())
    {}
};

// ════════════════════════════════════════════════════════
// File Transfer Events
// ════════════════════════════════════════════════════════

struct FileUploadStartedEvent {
    std::string session_id;
    std::string file_path;
    std::size_t total_bytes;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct FileChunkReceivedEvent {
    std::string session_id;
    std::string file_path;
    std::uint32_t chunk_index;
    std::uint32_t total_chunks;
    std::size_t bytes_received;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct FileUploadCompletedEvent {
    std::string session_id;
    std::string file_path;
    std::string hash;
    std::size_t total_bytes;
    std::chrono::milliseconds duration{0};
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct FileDownloadCompletedEvent {
    std::string session_id;
    std::string file_path;
    std::size_t total_bytes;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

// ════════════════════════════════════════════════════════
// Conflict Events
// ════════════════════════════════════════════════════════

struct FileConflictDetectedEvent {
    metadata::FileMetadata local;
    metadata::FileMetadata remote;
    std::string session_id;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct FileConflictResolvedEvent {
    metadata::FileMetadata resolved;
    metadata::FileMetadata other;
    ConflictResolutionStrategy strategy;
    std::string session_id;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

} // namespace dfs::events
