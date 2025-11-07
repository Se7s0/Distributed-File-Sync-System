#include "dfs/sync/change_detector.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace fs = std::filesystem;
using dfs::metadata::SyncState;
using dfs::sync::ChangeDetector;
using dfs::sync::FileChange;

namespace {

fs::path create_temp_dir() {
    static std::atomic<uint64_t> counter{0};
    const auto base = fs::temp_directory_path();
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto id = timestamp ^ (counter.fetch_add(1) << 8);
    auto unique = base / fs::path("dfs_sync_test_" + std::to_string(id));
    fs::create_directories(unique);
    return unique;
}

void write_file(const fs::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << content;
}

std::optional<std::uint32_t> find_local_version(const dfs::metadata::FileMetadata& metadata,
                                                const std::string& replica_id) {
    for (const auto& replica : metadata.replicas) {
        if (replica.replica_id == replica_id) {
            return replica.version;
        }
    }
    return std::nullopt;
}

} // namespace

class ChangeDetectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = create_temp_dir();
    }

    void TearDown() override {
        if (!root_.empty()) {
            fs::remove_all(root_);
        }
    }

    fs::path root_;
};

TEST_F(ChangeDetectorTest, DetectsAddedModifiedAndDeletedFiles) {
    const std::string replica_id = "client_a";
    ChangeDetector detector(replica_id);

    // Initial scan - nothing in directory
    auto initial = detector.scan_directory(root_);
    EXPECT_TRUE(initial.changes.empty());

    // Create new file
    const auto file = root_ / "note.txt";
    write_file(file, "hello world");

    auto added = detector.scan_directory(root_);
    ASSERT_EQ(added.changes.size(), 1u);
    const auto& add_change = added.changes.front();
    EXPECT_EQ(add_change.kind, FileChange::Kind::Added);
    EXPECT_EQ(add_change.path, "note.txt");
    EXPECT_FALSE(add_change.previous_metadata.has_value());
    EXPECT_EQ(add_change.base_version, 0u);
    ASSERT_TRUE(find_local_version(add_change.current_metadata, replica_id).has_value());
    EXPECT_EQ(find_local_version(add_change.current_metadata, replica_id).value(), 1u);
    EXPECT_EQ(add_change.current_metadata.sync_state, SyncState::MODIFIED);

    // Modify file
    write_file(file, "goodbye");

    auto modified = detector.scan_directory(root_);
    ASSERT_EQ(modified.changes.size(), 1u);
    const auto& mod_change = modified.changes.front();
    EXPECT_EQ(mod_change.kind, FileChange::Kind::Modified);
    EXPECT_EQ(mod_change.path, "note.txt");
    ASSERT_TRUE(mod_change.previous_metadata.has_value());
    EXPECT_EQ(mod_change.base_hash, mod_change.previous_metadata->hash);
    EXPECT_EQ(mod_change.base_version, 1u);
    ASSERT_TRUE(find_local_version(mod_change.current_metadata, replica_id).has_value());
    EXPECT_EQ(find_local_version(mod_change.current_metadata, replica_id).value(), 2u);

    // Delete file
    fs::remove(file);

    auto deleted = detector.scan_directory(root_);
    ASSERT_EQ(deleted.changes.size(), 1u);
    const auto& del_change = deleted.changes.front();
    EXPECT_EQ(del_change.kind, FileChange::Kind::Deleted);
    EXPECT_EQ(del_change.path, "note.txt");
    ASSERT_TRUE(del_change.previous_metadata.has_value());
    EXPECT_EQ(del_change.base_version, 2u);
    EXPECT_EQ(del_change.current_metadata.sync_state, SyncState::DELETED);
}
