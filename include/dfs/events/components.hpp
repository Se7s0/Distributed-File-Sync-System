/**
 * @file components.hpp
 * @brief Reusable event-driven components
 *
 * WHY THIS FILE EXISTS:
 * Provides ready-to-use components that demonstrate the event pattern.
 * These components react to file events and can be easily extended.
 *
 * EXAMPLE:
 * EventBus bus;
 * LoggerComponent logger(bus);
 * MetricsComponent metrics(bus);
 * // Components automatically react to events!
 */

#pragma once

#include "dfs/events/event_bus.hpp"
#include "dfs/events/events.hpp"
#include <spdlog/spdlog.h>
#include <atomic>
#include <mutex>
#include <optional>
#include <queue>

namespace dfs::events {

/**
 * @brief Logger component - logs all file events
 *
 * WHAT IT DOES:
 * Subscribes to all file events and logs them using spdlog.
 *
 * USAGE:
 * EventBus bus;
 * LoggerComponent logger(bus);
 * // Now all file events are automatically logged!
 */
class LoggerComponent {
public:
    explicit LoggerComponent(EventBus& bus) : bus_(bus) {
        // Subscribe to all file events
        bus_.subscribe<FileAddedEvent>([this](const FileAddedEvent& e) {
            on_file_added(e);
        });

        bus_.subscribe<FileModifiedEvent>([this](const FileModifiedEvent& e) {
            on_file_modified(e);
        });

        bus_.subscribe<FileDeletedEvent>([this](const FileDeletedEvent& e) {
            on_file_deleted(e);
        });

        bus_.subscribe<ServerStartedEvent>([this](const ServerStartedEvent& e) {
            on_server_started(e);
        });

        bus_.subscribe<ServerShuttingDownEvent>([this](const ServerShuttingDownEvent& e) {
            on_server_shutdown(e);
        });

        bus_.subscribe<FileUploadStartedEvent>([this](const FileUploadStartedEvent& e) {
            on_file_upload_started(e);
        });

        bus_.subscribe<FileChunkReceivedEvent>([this](const FileChunkReceivedEvent& e) {
            on_file_chunk_received(e);
        });

        bus_.subscribe<FileUploadCompletedEvent>([this](const FileUploadCompletedEvent& e) {
            on_file_upload_completed(e);
        });

        bus_.subscribe<FileDownloadCompletedEvent>([this](const FileDownloadCompletedEvent& e) {
            on_file_download_completed(e);
        });

        bus_.subscribe<FileConflictDetectedEvent>([this](const FileConflictDetectedEvent& e) {
            on_conflict_detected(e);
        });

        bus_.subscribe<FileConflictResolvedEvent>([this](const FileConflictResolvedEvent& e) {
            on_conflict_resolved(e);
        });
    }

private:
    void on_file_added(const FileAddedEvent& e) {
        spdlog::info("[FileAdded] path={} hash={} size={} source={}",
            e.metadata.file_path,
            e.metadata.hash,
            e.metadata.size,
            e.source);
    }

    void on_file_modified(const FileModifiedEvent& e) {
        spdlog::info("[FileModified] path={} old_hash={} new_hash={} old_size={} new_size={} source={}",
            e.file_path,
            e.old_hash,
            e.new_hash,
            e.old_size,
            e.new_size,
            e.source);
    }

    void on_file_deleted(const FileDeletedEvent& e) {
        spdlog::info("[FileDeleted] path={} source={}",
            e.file_path,
            e.source);
    }

    void on_server_started(const ServerStartedEvent& e) {
        spdlog::info("════════════════════════════════════════════");
        spdlog::info("Server started on port {}", e.port);
        spdlog::info("Event-driven architecture enabled");
        spdlog::info("════════════════════════════════════════════");
    }

    void on_server_shutdown(const ServerShuttingDownEvent& e) {
        spdlog::info("════════════════════════════════════════════");
        spdlog::info("Server shutting down: {}", e.reason);
        spdlog::info("════════════════════════════════════════════");
    }

    void on_file_upload_started(const FileUploadStartedEvent& e) {
        spdlog::info("[UploadStarted] session={} path={} bytes={}", e.session_id, e.file_path, e.total_bytes);
    }

    void on_file_chunk_received(const FileChunkReceivedEvent& e) {
        spdlog::debug("[ChunkReceived] session={} path={} chunk={}/{} bytes={}",
                      e.session_id, e.file_path, e.chunk_index + 1, e.total_chunks, e.bytes_received);
    }

    void on_file_upload_completed(const FileUploadCompletedEvent& e) {
        spdlog::info("[UploadCompleted] session={} path={} bytes={} hash={} duration={}ms",
                     e.session_id, e.file_path, e.total_bytes, e.hash, e.duration.count());
    }

    void on_file_download_completed(const FileDownloadCompletedEvent& e) {
        spdlog::info("[DownloadCompleted] session={} path={} bytes={}",
                     e.session_id, e.file_path, e.total_bytes);
    }

    void on_conflict_detected(const FileConflictDetectedEvent& e) {
        spdlog::warn("[ConflictDetected] session={} path={} local_hash={} remote_hash={}",
                     e.session_id, e.local.file_path, e.local.hash, e.remote.hash);
    }

    void on_conflict_resolved(const FileConflictResolvedEvent& e) {
        spdlog::info("[ConflictResolved] session={} path={} strategy={} winner_hash={}",
                     e.session_id, e.resolved.file_path,
                     static_cast<int>(e.strategy), e.resolved.hash);
    }

    EventBus& bus_;
};

/**
 * @brief Metrics component - tracks statistics
 *
 * WHAT IT DOES:
 * Tracks counts of file operations for monitoring/analytics.
 *
 * USAGE:
 * MetricsComponent metrics(bus);
 * // Later...
 * auto stats = metrics.get_stats();
 * std::cout << "Files added: " << stats.files_added << "\n";
 */
class MetricsComponent {
public:
    struct Stats {
        std::atomic<uint64_t> files_added{0};
        std::atomic<uint64_t> files_modified{0};
        std::atomic<uint64_t> files_deleted{0};
        std::atomic<uint64_t> total_bytes_added{0};
        std::atomic<uint64_t> total_bytes_modified{0};
        std::atomic<uint64_t> files_uploaded{0};
        std::atomic<uint64_t> bytes_uploaded{0};
        std::atomic<uint64_t> files_downloaded{0};
        std::atomic<uint64_t> bytes_downloaded{0};
        std::atomic<uint64_t> conflicts_detected{0};
        std::atomic<uint64_t> conflicts_resolved{0};
    };

    explicit MetricsComponent(EventBus& bus) : bus_(bus) {
        bus_.subscribe<FileAddedEvent>([this](const FileAddedEvent& e) {
            on_file_added(e);
        });

        bus_.subscribe<FileModifiedEvent>([this](const FileModifiedEvent& e) {
            on_file_modified(e);
        });

        bus_.subscribe<FileDeletedEvent>([this](const FileDeletedEvent& e) {
            on_file_deleted(e);
        });

        bus_.subscribe<FileUploadCompletedEvent>([this](const FileUploadCompletedEvent& e) {
            on_file_upload_completed(e);
        });

        bus_.subscribe<FileDownloadCompletedEvent>([this](const FileDownloadCompletedEvent& e) {
            on_file_download_completed(e);
        });

        bus_.subscribe<FileConflictDetectedEvent>([this](const FileConflictDetectedEvent&) {
            stats_.conflicts_detected++;
        });

        bus_.subscribe<FileConflictResolvedEvent>([this](const FileConflictResolvedEvent&) {
            stats_.conflicts_resolved++;
        });
    }

    const Stats& get_stats() const {
        return stats_;
    }

    void print_stats() const {
        spdlog::info("═══════════════════════════════════════");
        spdlog::info("Session Statistics:");
        spdlog::info("  Files added:     {}", stats_.files_added.load());
        spdlog::info("  Files modified:  {}", stats_.files_modified.load());
        spdlog::info("  Files deleted:   {}", stats_.files_deleted.load());
        spdlog::info("  Bytes added:     {}", stats_.total_bytes_added.load());
        spdlog::info("  Bytes modified:  {}", stats_.total_bytes_modified.load());
        spdlog::info("  Files uploaded:  {}", stats_.files_uploaded.load());
        spdlog::info("  Bytes uploaded:  {}", stats_.bytes_uploaded.load());
        spdlog::info("  Files downloaded:{}", stats_.files_downloaded.load());
        spdlog::info("  Bytes downloaded:{}", stats_.bytes_downloaded.load());
        spdlog::info("  Conflicts det.:  {}", stats_.conflicts_detected.load());
        spdlog::info("  Conflicts res.:  {}", stats_.conflicts_resolved.load());
        spdlog::info("═══════════════════════════════════════");
    }

private:
    void on_file_added(const FileAddedEvent& e) {
        stats_.files_added++;
        stats_.total_bytes_added += e.metadata.size;
    }

    void on_file_modified(const FileModifiedEvent& e) {
        stats_.files_modified++;
        stats_.total_bytes_modified += e.new_size;
    }

    void on_file_deleted(const FileDeletedEvent& e) {
        stats_.files_deleted++;
    }

    void on_file_upload_completed(const FileUploadCompletedEvent& e) {
        stats_.files_uploaded++;
        stats_.bytes_uploaded += e.total_bytes;
    }

    void on_file_download_completed(const FileDownloadCompletedEvent& e) {
        stats_.files_downloaded++;
        stats_.bytes_downloaded += e.total_bytes;
    }

    EventBus& bus_;
    Stats stats_;
};

/**
 * @brief Sync component - queues files for synchronization
 *
 * WHAT IT DOES:
 * Listens to file events and queues files that need to be synced.
 *
 * USAGE:
 * SyncComponent sync(bus);
 * // When files are added/modified, they're automatically queued
 * if (sync.has_pending()) {
 *     auto file = sync.next();
 *     // Sync the file...
 * }
 */
class SyncComponent {
public:
    explicit SyncComponent(EventBus& bus) : bus_(bus) {
        bus_.subscribe<FileAddedEvent>([this](const FileAddedEvent& e) {
            on_file_added(e);
        });

        bus_.subscribe<FileModifiedEvent>([this](const FileModifiedEvent& e) {
            on_file_modified(e);
        });
    }

    size_t queue_size() const {
        std::lock_guard lock(mutex_);
        return sync_queue_.size();
    }

    bool has_pending() const {
        std::lock_guard lock(mutex_);
        return !sync_queue_.empty();
    }

    std::optional<std::string> next() {
        std::lock_guard lock(mutex_);
        if (sync_queue_.empty()) {
            return std::nullopt;
        }

        std::string path = sync_queue_.front();
        sync_queue_.pop();
        return path;
    }

private:
    void on_file_added(const FileAddedEvent& e) {
        std::lock_guard lock(mutex_);
        sync_queue_.push(e.metadata.file_path);
        spdlog::debug("Queued for sync: {} (added)", e.metadata.file_path);
    }

    void on_file_modified(const FileModifiedEvent& e) {
        std::lock_guard lock(mutex_);
        sync_queue_.push(e.file_path);
        spdlog::debug("Queued for sync: {} (modified)", e.file_path);
    }

    EventBus& bus_;
    mutable std::mutex mutex_;
    std::queue<std::string> sync_queue_;
};

} // namespace dfs::events
