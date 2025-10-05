#pragma once

/**
 * @file store.hpp
 * @brief In-memory metadata storage with thread-safe operations
 *
 * WHY THIS FILE EXISTS:
 * We need a centralized place to store and retrieve file metadata.
 * Think of it as an in-memory database for FileMetadata structs.
 *
 * WHAT PROBLEM IT SOLVES:
 * - Fast lookups: Find metadata by file path in O(1) time
 * - Thread safety: Multiple HTTP requests can access store concurrently
 * - CRUD operations: Create, Read, Update, Delete metadata
 * - Listing: Get all metadata for client synchronization
 *
 * HOW IT INTEGRATES:
 * - HTTP endpoints call store methods to get/save metadata
 * - Parser creates FileMetadata → store saves it
 * - Serializer retrieves FileMetadata from store → converts to binary
 * - Multiple worker threads in HTTP server access store simultaneously
 *
 * DESIGN DECISIONS:
 * - std::unordered_map for O(1) lookups by file path
 * - std::shared_mutex for reader-writer lock (many reads, few writes)
 * - In-memory only in Phase 2 (disk persistence comes in Phase 3-4)
 * - File path as key (unique identifier for files)
 *
 * THREAD SAFETY PATTERN:
 * - Multiple readers can read simultaneously (std::shared_lock)
 * - Only one writer can write at a time (std::unique_lock)
 * - Readers block writers, writers block everyone
 *
 * EXAMPLE USAGE:
 * MetadataStore store;
 *
 * // Thread 1 (HTTP worker): Add metadata
 * FileMetadata metadata;
 * metadata.file_path = "/test.txt";
 * store.add(metadata);
 *
 * // Thread 2 (HTTP worker): Read metadata (concurrent with Thread 3!)
 * auto result = store.get("/test.txt");
 *
 * // Thread 3 (HTTP worker): Read metadata (concurrent with Thread 2!)
 * auto all = store.list_all();
 *
 * WHY IN-MEMORY NOW, DATABASE LATER:
 * Phase 2: In-memory (simple, fast, learn the concepts)
 * Phase 3-4: Add disk persistence (save to file on shutdown)
 * Phase 6: Real database (SQLite → PostgreSQL)
 */

#include "dfs/metadata/types.hpp"
#include "dfs/core/result.hpp"
#include <unordered_map>
#include <shared_mutex>
#include <vector>
#include <string>

namespace dfs {
namespace metadata {

/**
 * @brief Thread-safe in-memory metadata storage
 *
 * WHY THIS CLASS:
 * Central repository for all file metadata in the system.
 * Provides thread-safe access for concurrent HTTP requests.
 *
 * INTERNAL DATA STRUCTURE:
 * std::unordered_map<std::string, FileMetadata>
 *   Key: file_path (e.g., "/docs/project.txt")
 *   Value: FileMetadata struct
 *
 * CONCURRENCY MODEL:
 * Uses reader-writer lock (std::shared_mutex):
 * - Read operations (get, list_all, exists): shared_lock (multiple concurrent readers)
 * - Write operations (add, update, remove): unique_lock (exclusive access)
 *
 * WHY READER-WRITER LOCK:
 * In a file sync system, reads are much more common than writes:
 * - 1000s of "check if file changed" requests per second (reads)
 * - 10s of "file was modified" updates per second (writes)
 * Reader-writer lock optimizes for this read-heavy workload.
 */
class MetadataStore {
public:
    /**
     * Constructor
     * WHY: Initialize empty metadata store
     */
    MetadataStore() = default;

    /**
     * Add new file metadata to store
     *
     * WHY THIS METHOD:
     * When a client uploads a new file, we need to store its metadata.
     *
     * HOW IT WORKS:
     * 1. Acquire exclusive lock (unique_lock) - blocks all other operations
     * 2. Check if file already exists
     * 3. If exists: return error (use update() instead)
     * 4. If not exists: add to map
     * 5. Release lock
     *
     * EXAMPLE:
     * FileMetadata metadata;
     * metadata.file_path = "/new_file.txt";
     * metadata.hash = "abc123";
     * auto result = store.add(metadata);
     * if (result.is_error()) {
     *     // File already exists!
     * }
     *
     * @param metadata FileMetadata to add
     * @return Result<void> - success or error if already exists
     */
    Result<void> add(const FileMetadata& metadata) {
        std::unique_lock lock(mutex_);  // Exclusive lock (blocks everyone)

        // Check if already exists
        if (metadata_.find(metadata.file_path) != metadata_.end()) {
            return Err<void, std::string>(
                "File already exists: " + metadata.file_path
            );
        }

        // Add to store
        metadata_[metadata.file_path] = metadata;

        return Ok();
    }

    /**
     * Get metadata for a specific file
     *
     * WHY THIS METHOD:
     * Most common operation - clients need to check if their files are up-to-date.
     *
     * HOW IT WORKS:
     * 1. Acquire shared lock (shared_lock) - allows concurrent readers
     * 2. Look up file path in map
     * 3. If found: return metadata
     * 4. If not found: return error
     * 5. Release lock
     *
     * EXAMPLE:
     * auto result = store.get("/test.txt");
     * if (result.is_ok()) {
     *     FileMetadata metadata = result.value();
     *     // Compare with client's version...
     * } else {
     *     // File not found in store
     * }
     *
     * @param file_path Path to file
     * @return Result<FileMetadata> - metadata if found, error otherwise
     */
    Result<FileMetadata> get(const std::string& file_path) const {
        std::shared_lock lock(mutex_);  // Shared lock (allows concurrent reads)

        auto it = metadata_.find(file_path);
        if (it == metadata_.end()) {
            return Err<FileMetadata, std::string>(
                "File not found: " + file_path
            );
        }

        return Ok(it->second);
    }

    /**
     * Update existing metadata
     *
     * WHY THIS METHOD:
     * When a file is modified, we need to update its metadata
     * (new hash, new modified_time, new version, etc.)
     *
     * HOW IT WORKS:
     * 1. Acquire exclusive lock (blocks everyone)
     * 2. Check if file exists
     * 3. If not exists: return error (use add() instead)
     * 4. If exists: replace with new metadata
     * 5. Release lock
     *
     * EXAMPLE:
     * auto result = store.get("/test.txt");
     * FileMetadata metadata = result.value();
     * metadata.hash = "new_hash_after_edit";
     * metadata.modified_time = std::time(nullptr);
     * metadata.update_replica("laptop_1", 6, std::time(nullptr));
     * store.update(metadata);
     *
     * @param metadata Updated metadata
     * @return Result<void> - success or error if not found
     */
    Result<void> update(const FileMetadata& metadata) {
        std::unique_lock lock(mutex_);  // Exclusive lock

        // Check if exists
        auto it = metadata_.find(metadata.file_path);
        if (it == metadata_.end()) {
            return Err<void, std::string>(
                "File not found: " + metadata.file_path
            );
        }

        // Update
        it->second = metadata;

        return Ok();
    }

    /**
     * Add or update metadata (upsert operation)
     *
     * WHY THIS METHOD:
     * Sometimes we don't know if metadata already exists.
     * This method handles both cases - if exists: update, if not: add.
     *
     * HOW IT WORKS:
     * 1. Acquire exclusive lock
     * 2. Insert or update in map (operator[] does this automatically)
     * 3. Release lock
     *
     * EXAMPLE:
     * // Don't care if it exists or not, just save it
     * store.add_or_update(metadata);
     *
     * @param metadata Metadata to add or update
     */
    void add_or_update(const FileMetadata& metadata) {
        std::unique_lock lock(mutex_);
        metadata_[metadata.file_path] = metadata;
    }

    /**
     * Remove metadata from store
     *
     * WHY THIS METHOD:
     * When a file is deleted, we remove its metadata.
     * (Or mark it as DELETED state for sync tombstone)
     *
     * HOW IT WORKS:
     * 1. Acquire exclusive lock
     * 2. Remove from map
     * 3. Release lock
     *
     * EXAMPLE:
     * store.remove("/test.txt");
     *
     * @param file_path Path to file to remove
     * @return Result<void> - success or error if not found
     */
    Result<void> remove(const std::string& file_path) {
        std::unique_lock lock(mutex_);

        auto it = metadata_.find(file_path);
        if (it == metadata_.end()) {
            return Err<void, std::string>(
                "File not found: " + file_path
            );
        }

        metadata_.erase(it);

        return Ok();
    }

    /**
     * Check if file exists in store
     *
     * WHY THIS METHOD:
     * Quick check before performing operations.
     *
     * HOW IT WORKS:
     * 1. Acquire shared lock (allows concurrent checks)
     * 2. Check if key exists in map
     * 3. Release lock
     *
     * EXAMPLE:
     * if (store.exists("/test.txt")) {
     *     // File is tracked
     * } else {
     *     // New file, need to add metadata
     * }
     *
     * @param file_path Path to check
     * @return true if exists, false otherwise
     */
    bool exists(const std::string& file_path) const {
        std::shared_lock lock(mutex_);
        return metadata_.find(file_path) != metadata_.end();
    }

    /**
     * Get all metadata in store
     *
     * WHY THIS METHOD:
     * Client synchronization: "Give me list of all files on server"
     * Client compares server list with local list to detect changes.
     *
     * HOW IT WORKS:
     * 1. Acquire shared lock (allows concurrent reads)
     * 2. Copy all metadata to vector
     * 3. Release lock
     * 4. Return vector
     *
     * EXAMPLE:
     * auto all_metadata = store.list_all();
     * for (const auto& metadata : all_metadata) {
     *     std::cout << metadata.file_path << " : " << metadata.hash << std::endl;
     * }
     *
     * NOTE: Returns a COPY of all metadata (safe to use after lock is released)
     *
     * @return Vector of all FileMetadata in store
     */
    std::vector<FileMetadata> list_all() const {
        std::shared_lock lock(mutex_);

        std::vector<FileMetadata> result;
        result.reserve(metadata_.size());

        for (const auto& [path, metadata] : metadata_) {
            result.push_back(metadata);
        }

        return result;
    }

    /**
     * Get count of files in store
     *
     * WHY THIS METHOD:
     * For statistics, monitoring, debugging.
     *
     * EXAMPLE:
     * std::cout << "Tracking " << store.size() << " files" << std::endl;
     *
     * @return Number of files in store
     */
    size_t size() const {
        std::shared_lock lock(mutex_);
        return metadata_.size();
    }

    /**
     * Clear all metadata from store
     *
     * WHY THIS METHOD:
     * For testing, resetting system, emergency cleanup.
     *
     * WARNING: This removes ALL metadata! Use with caution.
     */
    void clear() {
        std::unique_lock lock(mutex_);
        metadata_.clear();
    }

    /**
     * Get all metadata matching a predicate
     *
     * WHY THIS METHOD:
     * For querying: "Give me all files modified after timestamp X"
     * or "Give me all files in CONFLICT state"
     *
     * HOW IT WORKS:
     * 1. Acquire shared lock
     * 2. Iterate through all metadata
     * 3. If predicate returns true, add to result
     * 4. Release lock and return result
     *
     * EXAMPLE:
     * // Get all files in conflict state
     * auto conflicts = store.query([](const FileMetadata& m) {
     *     return m.sync_state == SyncState::CONFLICT;
     * });
     *
     * // Get all files modified after Jan 1, 2024
     * time_t cutoff = 1704067200;
     * auto recent = store.query([cutoff](const FileMetadata& m) {
     *     return m.modified_time > cutoff;
     * });
     *
     * @param predicate Function that returns true for metadata to include
     * @return Vector of matching FileMetadata
     */
    template<typename Predicate>
    std::vector<FileMetadata> query(Predicate predicate) const {
        std::shared_lock lock(mutex_);

        std::vector<FileMetadata> result;

        for (const auto& [path, metadata] : metadata_) {
            if (predicate(metadata)) {
                result.push_back(metadata);
            }
        }

        return result;
    }

private:
    /**
     * Internal storage: file_path → FileMetadata
     *
     * WHY unordered_map:
     * - O(1) average case lookup by file path
     * - Don't need ordering (unlike std::map)
     * - File paths are unique (good hash distribution)
     */
    mutable std::shared_mutex mutex_;  // Reader-writer lock
    std::unordered_map<std::string, FileMetadata> metadata_;

    /**
     * THREAD SAFETY VISUALIZATION:
     *
     * Time →
     * Thread 1: [  get("/a")  ]     [ add("/c") blocks! ]
     * Thread 2:      [ get("/b") ]  [ waiting...        ]
     * Thread 3:         [ list_all() ]
     * Thread 4:                      [add("/d") - exclusive!]
     *           └─ shared lock ─┘    └── unique lock ──┘
     *           Multiple readers OK   Only one writer
     *
     * Reader-writer lock allows:
     * - Multiple get/list_all simultaneously (shared_lock)
     * - Only one add/update/remove at a time (unique_lock)
     * - Writers wait for readers, readers wait for writers
     */
};

} // namespace metadata
} // namespace dfs
