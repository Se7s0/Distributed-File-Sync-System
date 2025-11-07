#include "dfs/sync/merkle_tree.hpp"

#include <gtest/gtest.h>

using dfs::metadata::FileMetadata;
using dfs::sync::MerkleTree;

namespace {

FileMetadata make_metadata(const std::string& path, const std::string& hash, std::size_t size = 0) {
    FileMetadata metadata;
    metadata.file_path = path;
    metadata.hash = hash;
    metadata.size = size;
    return metadata;
}

} // namespace

TEST(MerkleTreeTest, IdenticalTreesProduceEmptyDiff) {
    MerkleTree tree_a;
    MerkleTree tree_b;

    std::vector<FileMetadata> files {
        make_metadata("/a.txt", "hashA", 100),
        make_metadata("/b.txt", "hashB", 42)
    };

    tree_a.build(files);
    tree_b.build(files);

    EXPECT_EQ(tree_a.root_hash(), tree_b.root_hash());
    EXPECT_TRUE(tree_a.diff(tree_b).empty());
}

TEST(MerkleTreeTest, DetectsAddedAndModifiedFiles) {
    MerkleTree tree_original;
    MerkleTree tree_updated;

    tree_original.build({
        make_metadata("/a.txt", "hashA", 100),
        make_metadata("/b.txt", "hashB", 42)
    });

    tree_updated.build({
        make_metadata("/a.txt", "hashA", 100),
        make_metadata("/b.txt", "newHashB", 42),
        make_metadata("/c.txt", "hashC", 64)
    });

    const auto diff = tree_original.diff(tree_updated);
    ASSERT_EQ(diff.size(), 2u);
    EXPECT_EQ(diff[0], "/b.txt");
    EXPECT_EQ(diff[1], "/c.txt");
}

TEST(MerkleTreeTest, DetectsRemovedFiles) {
    MerkleTree tree_before;
    MerkleTree tree_after;

    tree_before.build({
        make_metadata("/a.txt", "hashA", 100),
        make_metadata("/b.txt", "hashB", 42)
    });

    tree_after.build({
        make_metadata("/a.txt", "hashA", 100)
    });

    const auto diff = tree_after.diff(tree_before);
    ASSERT_EQ(diff.size(), 1u);
    EXPECT_EQ(diff[0], "/b.txt");
}
