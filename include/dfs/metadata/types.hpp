#pragma once

/**
 * @file types.hpp
 * @brief Core metadata types for the Distributed File Sync System
 *
 * WHY THIS FILE EXISTS:
 * In a distributed file sync system (like Dropbox), we need to track metadata
 * about files across multiple devices. Instead of comparing entire files to
 * detect changes (expensive!), we compare metadata (fast!).
 *
 * WHAT PROBLEM IT SOLVES:
 * - Efficient change detection: Compare hash instead of file contents
 * - Conflict resolution: Track which version is newer, who modified it
 * - Replica management: Know which devices have which versions
 * - Sync coordination: Understand sync state across distributed system
 *
 * HOW IT INTEGRATES:
 * - Parser (metadata/parser.hpp) creates these structs from DDL text
 * - Store (metadata/store.hpp) stores and retrieves these structs
 * - Serializer (metadata/serializer.hpp) converts to/from binary for network
 * - HTTP API uses these to respond to sync requests from clients
 *
 * DESIGN DECISIONS:
 * - Use structs instead of classes: Simple data containers, no complex behavior
 * - std::string for hash: Hex representation (64 chars for SHA-256)
 * - time_t for timestamps: Standard C time, easy to serialize
 * - enum for SyncState: Type-safe state machine for sync process
 */

#include <string>
#include <vector>
#include <ctime>
#include <cstdint>

namespace dfs {
namespace metadata {

/**
 * @brief Sync state for a file in the distributed system
 *
 * WHY THIS EXISTS:
 * Files in a distributed sync system go through different states.
 * We need to track these states to coordinate sync operations.
 *
 * STATE TRANSITIONS:
 * SYNCED → MODIFIED (user edits file)
 * MODIFIED → SYNCING (sync starts)
 * SYNCING → SYNCED (sync completes)
 * SYNCING → CONFLICT (another device modified same file)
 *
 * EXAMPLE:
 * 1. File on laptop is SYNCED
 * 2. User edits it → MODIFIED
 * 3. Laptop syncs to server → SYNCING
 * 4. Server confirms → SYNCED
 * 5. If phone also edited same file → CONFLICT
 */
enum class SyncState {
    SYNCED,      // File is up-to-date on all replicas
    MODIFIED,    // File has been changed locally, needs sync
    SYNCING,     // Sync operation in progress
    CONFLICT,    // Multiple conflicting versions exist
    DELETED      // File has been deleted (tombstone for sync)
};

/**
 * @brief Information about a single replica (copy) of a file
 *
 * WHY THIS EXISTS:
 * In a distributed system, the same file exists on multiple devices (replicas).
 * We need to track which device has which version, and when it was last updated.
 *
 * HOW IT'S USED:
 * - When device A modifies a file, its replica_id and modified_time update
 * - Other devices compare their modified_time to detect they're out of date
 * - Server uses this to route sync requests to the latest replica
 *
 * EXAMPLE:
 * File "project.txt" has 3 replicas:
 * - laptop_1: version=5, modified=Jan 1 10:00
 * - phone_1: version=4, modified=Dec 31 09:00  (out of date!)
 * - desktop_1: version=5, modified=Jan 1 10:00
 */
struct ReplicaInfo {
    std::string replica_id;   // Device identifier (e.g., "laptop_1", "phone_1")
    uint32_t version;         // Version number (increments on each modification)
    time_t modified_time;     // Last modification timestamp (Unix epoch)

    /**
     * Default constructor
     * WHY: Allows creating empty ReplicaInfo for initialization
     */
    ReplicaInfo()
        : version(0), modified_time(0) {}

    /**
     * Full constructor
     * WHY: Convenient way to create ReplicaInfo with all fields
     */
    ReplicaInfo(const std::string& id, uint32_t ver, time_t mtime)
        : replica_id(id), version(ver), modified_time(mtime) {}
};

/**
 * @brief Complete metadata for a single file
 *
 * WHY THIS EXISTS:
 * This is the CORE data structure of Phase 2. It contains everything we need
 * to know about a file without actually reading the file contents.
 *
 * HOW IT ENABLES EFFICIENT SYNC:
 * 1. Client asks server: "I have project.txt with hash abc123, size 1024, modified Jan 1"
 * 2. Server compares hash: If different → file changed, sync needed
 * 3. Server compares modified_time: If server newer → download, if client newer → upload
 * 4. No need to transfer entire file just to detect changes!
 *
 * FIELDS EXPLAINED:
 * - file_path: Unique identifier for the file (e.g., "/docs/project.txt")
 * - hash: SHA-256 hash of file contents (for detecting changes)
 * - size: File size in bytes (for progress bars, quota checks)
 * - modified_time: When file was last modified (for conflict resolution)
 * - created_time: When file was created (for sorting, metadata)
 * - sync_state: Current state in sync process (SYNCED, MODIFIED, etc.)
 * - replicas: List of all devices that have this file
 *
 * EXAMPLE DDL:
 * FILE "/docs/project.txt"
 *   HASH "a1b2c3d4..."
 *   SIZE 1024
 *   MODIFIED 1704096000
 *   CREATED 1704000000
 *   STATE SYNCED
 *   REPLICA "laptop_1" VERSION 5 MODIFIED 1704096000
 *   REPLICA "phone_1" VERSION 4 MODIFIED 1703000000
 */
struct FileMetadata {
    // File identification
    std::string file_path;        // Full path to file (e.g., "/docs/project.txt")

    // File content fingerprint
    std::string hash;             // SHA-256 hash (hex string, 64 chars)
    uint64_t size;                // File size in bytes

    // Timestamps
    time_t modified_time;         // Last modification time (Unix epoch)
    time_t created_time;          // Creation time (Unix epoch)

    // Sync coordination
    SyncState sync_state;         // Current sync state
    std::vector<ReplicaInfo> replicas;  // All replicas of this file

    /**
     * Default constructor
     * WHY: Allows creating empty FileMetadata for initialization
     */
    FileMetadata()
        : size(0), modified_time(0), created_time(0),
          sync_state(SyncState::SYNCED) {}

    /**
     * Check if this metadata represents a newer version than another
     *
     * WHY THIS METHOD:
     * During sync, we need to determine which version is newer.
     * We use modified_time as the primary indicator.
     *
     * HOW IT'S USED:
     * if (server_metadata.is_newer_than(client_metadata)) {
     *     // Server has newer version, client should download
     * } else {
     *     // Client has newer version, client should upload
     * }
     */
    bool is_newer_than(const FileMetadata& other) const {
        return modified_time > other.modified_time;
    }

    /**
     * Check if this file has conflicts (multiple replicas with different versions)
     *
     * WHY THIS METHOD:
     * Conflicts occur when multiple devices modify the same file offline.
     * We need to detect this to prompt user for conflict resolution.
     *
     * HOW IT'S USED:
     * if (metadata.has_conflict()) {
     *     // Show user: "project.txt has conflicts, choose which version to keep"
     * }
     */
    bool has_conflict() const {
        if (replicas.size() <= 1) return false;

        // Check if all replicas have same version
        uint32_t first_version = replicas[0].version;
        for (size_t i = 1; i < replicas.size(); ++i) {
            if (replicas[i].version != first_version) {
                return true;  // Different versions → conflict
            }
        }
        return false;
    }

    /**
     * Get the latest replica (most recent modification)
     *
     * WHY THIS METHOD:
     * When syncing, we want to fetch from the replica with the latest version.
     *
     * HOW IT'S USED:
     * auto latest = metadata.get_latest_replica();
     * // Connect to latest.replica_id to download newest version
     */
    const ReplicaInfo* get_latest_replica() const {
        if (replicas.empty()) return nullptr;

        const ReplicaInfo* latest = &replicas[0];
        for (size_t i = 1; i < replicas.size(); ++i) {
            if (replicas[i].modified_time > latest->modified_time) {
                latest = &replicas[i];
            }
        }
        return latest;
    }

    /**
     * Add or update a replica
     *
     * WHY THIS METHOD:
     * When a device syncs a file, we need to update its replica info.
     * If replica already exists, update it. If not, add it.
     *
     * HOW IT'S USED:
     * metadata.update_replica("laptop_1", 6, current_time);
     * // Now laptop_1 has version 6
     */
    void update_replica(const std::string& replica_id, uint32_t version, time_t mtime) {
        // Find existing replica
        for (auto& replica : replicas) {
            if (replica.replica_id == replica_id) {
                replica.version = version;
                replica.modified_time = mtime;
                return;
            }
        }

        // Not found, add new replica
        replicas.emplace_back(replica_id, version, mtime);
    }
};

/**
 * @brief Helper functions for SyncState enum
 *
 * WHY THIS EXISTS:
 * We need to convert between SyncState enum and string representations
 * for parsing DDL and displaying to users.
 */
class SyncStateUtils {
public:
    /**
     * Convert string to SyncState enum
     *
     * WHY: Parser needs to convert DDL text like "SYNCED" to enum value
     */
    static SyncState from_string(const std::string& state_str) {
        if (state_str == "SYNCED") return SyncState::SYNCED;
        if (state_str == "MODIFIED") return SyncState::MODIFIED;
        if (state_str == "SYNCING") return SyncState::SYNCING;
        if (state_str == "CONFLICT") return SyncState::CONFLICT;
        if (state_str == "DELETED") return SyncState::DELETED;
        return SyncState::SYNCED;  // Default fallback
    }

    /**
     * Convert SyncState enum to string
     *
     * WHY: For logging, debugging, and sending to clients
     */
    static std::string to_string(SyncState state) {
        switch (state) {
            case SyncState::SYNCED: return "SYNCED";
            case SyncState::MODIFIED: return "MODIFIED";
            case SyncState::SYNCING: return "SYNCING";
            case SyncState::CONFLICT: return "CONFLICT";
            case SyncState::DELETED: return "DELETED";
            default: return "UNKNOWN";
        }
    }
};

} // namespace metadata
} // namespace dfs
