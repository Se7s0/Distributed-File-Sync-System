#include "dfs/sync/conflict.hpp"

#include <gtest/gtest.h>

#include <ctime>

using dfs::metadata::FileMetadata;
using dfs::metadata::SyncState;
using dfs::sync::ConflictResolver;
using dfs::events::ConflictResolutionStrategy;

namespace {

FileMetadata make_metadata(const std::string& path, std::string hash, std::time_t mtime) {
    FileMetadata metadata;
    metadata.file_path = path;
    metadata.hash = std::move(hash);
    metadata.modified_time = mtime;
    metadata.sync_state = SyncState::MODIFIED;
    return metadata;
}

} // namespace

TEST(ConflictResolverTest, LastWriteWinsChoosesNewest) {
    ConflictResolver resolver;
    auto local = make_metadata("/doc.txt", "hash-old", 100);
    auto remote = make_metadata("/doc.txt", "hash-new", 200);

    auto result = resolver.resolve(local, remote, ConflictResolutionStrategy::LastWriteWins);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().resolved.hash, "hash-new");
    EXPECT_EQ(result.value().other.hash, "hash-old");
    EXPECT_FALSE(result.value().requires_manual_attention);
}

TEST(ConflictResolverTest, ManualRequiresHumanIntervention) {
    ConflictResolver resolver;
    auto local = make_metadata("/doc.txt", "hash-a", 100);
    auto remote = make_metadata("/doc.txt", "hash-b", 150);

    auto result = resolver.resolve(local, remote, ConflictResolutionStrategy::Manual);
    EXPECT_TRUE(result.is_error());
}

TEST(ConflictResolverTest, MergeNotImplemented) {
    ConflictResolver resolver;
    auto local = make_metadata("/doc.txt", "hash-a", 100);
    auto remote = make_metadata("/doc.txt", "hash-b", 150);

    auto result = resolver.resolve(local, remote, ConflictResolutionStrategy::Merge);
    EXPECT_TRUE(result.is_error());
}
