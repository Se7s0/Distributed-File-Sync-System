#include "dfs/events/event_bus.hpp"
#include "dfs/events/components.hpp"
#include "dfs/events/events.hpp"

#include <gtest/gtest.h>

#include <chrono>

using dfs::events::EventBus;
using dfs::events::FileUploadCompletedEvent;
using dfs::events::FileDownloadCompletedEvent;
using dfs::events::FileConflictDetectedEvent;
using dfs::events::FileConflictResolvedEvent;
using dfs::events::MetricsComponent;
using dfs::events::ConflictResolutionStrategy;

TEST(MetricsComponentTest, TracksTransferAndConflictCounters) {
    EventBus bus;
    MetricsComponent metrics(bus);

    FileUploadCompletedEvent upload_event{"session-1", "/file.txt", "hash", 1024, std::chrono::milliseconds{200}};
    bus.emit(upload_event);

    FileDownloadCompletedEvent download_event{"session-1", "/file.txt", 2048};
    bus.emit(download_event);

    FileConflictDetectedEvent conflict_detected{dfs::metadata::FileMetadata{}, dfs::metadata::FileMetadata{}, "session-1"};
    bus.emit(conflict_detected);

    FileConflictResolvedEvent conflict_resolved{dfs::metadata::FileMetadata{}, dfs::metadata::FileMetadata{}, ConflictResolutionStrategy::LastWriteWins, "session-1"};
    bus.emit(conflict_resolved);

    const auto& stats = metrics.get_stats();
    EXPECT_EQ(stats.files_uploaded.load(), 1u);
    EXPECT_EQ(stats.bytes_uploaded.load(), 1024u);
    EXPECT_EQ(stats.files_downloaded.load(), 1u);
    EXPECT_EQ(stats.bytes_downloaded.load(), 2048u);
    EXPECT_EQ(stats.conflicts_detected.load(), 1u);
    EXPECT_EQ(stats.conflicts_resolved.load(), 1u);
}
