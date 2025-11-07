#include "dfs/sync/transfer.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using dfs::sync::ChunkEnvelope;
using dfs::sync::FileTransferService;

namespace {

fs::path create_temp_dir() {
    const auto base = fs::temp_directory_path();
    static std::atomic<uint64_t> counter{0};
    const auto id = counter.fetch_add(1);
    fs::path dir = base / fs::path("dfs_transfer_test_" + std::to_string(id));
    fs::create_directories(dir);
    return dir;
}

std::string read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream oss;
    oss << input.rdbuf();
    return oss.str();
}

std::string hash_string(const std::string& data) {
    const std::size_t raw_hash = std::hash<std::string>{}(data);
    std::ostringstream hex;
    hex << std::hex << std::setw(sizeof(raw_hash) * 2) << std::setfill('0') << raw_hash;
    return hex.str();
}

} // namespace

TEST(FileTransferServiceTest, UploadAndApplyChunks) {
    const auto source_dir = create_temp_dir();
    const auto staging_dir = create_temp_dir();
    const auto destination_dir = create_temp_dir();

    const fs::path file = source_dir / "example.bin";
    {
        std::ofstream out(file, std::ios::binary);
        out << "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor incididunt.";
    }

    FileTransferService service;
    std::vector<ChunkEnvelope> captured;

    auto sink = [&](ChunkEnvelope&& chunk) -> dfs::Result<void> {
        captured.push_back(std::move(chunk));
        return dfs::Ok();
    };

    ASSERT_TRUE(service.upload_file(file, "session-1", "example.bin", sink, 16).is_ok());
    ASSERT_FALSE(captured.empty());

    for (const auto& chunk : captured) {
        ASSERT_TRUE(service.apply_chunk(chunk, staging_dir).is_ok());
    }

    const std::string original_content = read_file(file);
    const std::string expected_hash = hash_string(original_content);

    ASSERT_TRUE(service.finalize_file("session-1", "example.bin", staging_dir, destination_dir, expected_hash).is_ok());

    const std::string rebuilt = read_file(destination_dir / "example.bin");
    EXPECT_EQ(rebuilt, original_content);
}

TEST(FileTransferServiceTest, DetectsCorruptedChunk) {
    FileTransferService service;
    ChunkEnvelope chunk;
    chunk.session_id = "session-2";
    chunk.file_path = "file.txt";
    chunk.chunk_index = 0;
    chunk.total_chunks = 1;
    chunk.chunk_size = 16;
    chunk.data.assign({'B', 'a', 'd'});
    chunk.chunk_hash = "deadbeef"; // incorrect on purpose

    const auto staging_dir = create_temp_dir();
    auto result = service.apply_chunk(chunk, staging_dir);
    EXPECT_TRUE(result.is_error());
}
