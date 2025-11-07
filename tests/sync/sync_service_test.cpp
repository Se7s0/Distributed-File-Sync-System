#include "dfs/sync/service.hpp"
#include "dfs/events/event_bus.hpp"
#include "dfs/events/components.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;
using dfs::sync::SyncService;
using dfs::metadata::MetadataStore;
using dfs::events::EventBus;

namespace {

fs::path create_temp_dir(const std::string& prefix) {
    auto base = fs::temp_directory_path();
    static std::atomic<uint64_t> counter{0};
    auto dir = base / fs::path(prefix + std::to_string(counter.fetch_add(1)));
    fs::create_directories(dir);
    return dir;
}

std::string write_file(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
    return content;
}

std::vector<dfs::metadata::FileMetadata> snapshot_from_store(MetadataStore& store) {
    return store.list_all();
}

} // namespace

TEST(SyncServiceTest, UploadLifecycleCompletesSession) {
    EventBus bus;
    MetadataStore store;
    auto data_root = create_temp_dir("dfs_sync_data_test");
    auto staging_root = create_temp_dir("dfs_sync_stage_test");

    dfs::events::LoggerComponent logger(bus);
    dfs::events::MetricsComponent metrics(bus);
    dfs::events::SyncComponent sync_component(bus);

    SyncService service(data_root, staging_root, bus, store);

    const auto client = service.register_client();
    auto session_info = service.start_session(client);
    ASSERT_TRUE(session_info.is_ok());
    const auto session_id = session_info.value().session_id;

    // Simulate uploading a file using transfer service helpers
    const fs::path temp_file = staging_root / "upload_source.txt";
    const std::string content = "example payload";
    write_file(temp_file, content);

    dfs::metadata::FileMetadata local_meta;
    local_meta.file_path = "docs/note.txt";
    local_meta.hash = [] (const std::string& text) {
        const std::size_t raw = std::hash<std::string>{}(text);
        std::ostringstream hex;
        hex << std::hex << std::setw(sizeof(raw) * 2) << std::setfill('0') << raw;
        return hex.str();
    }(content);
    local_meta.size = content.size();

    auto diff = service.compute_diff(session_id, {local_meta});
    ASSERT_TRUE(diff.is_ok());
    ASSERT_EQ(diff.value().files_to_upload.size(), 1u);

    dfs::sync::FileTransferService transfer;
    std::vector<dfs::sync::ChunkEnvelope> chunks;
    auto sink = [&](dfs::sync::ChunkEnvelope&& envelope) {
        chunks.push_back(std::move(envelope));
        return dfs::Ok();
    };
    ASSERT_TRUE(transfer.upload_file(temp_file, session_id, "docs/note.txt", sink, 8).is_ok());

    for (auto& chunk : chunks) {
        ASSERT_TRUE(service.ingest_chunk(chunk).is_ok());
    }

    auto finalize = service.finalize_upload(session_id, "docs/note.txt", local_meta.hash);
    ASSERT_TRUE(finalize.is_ok());

    auto info = service.session_info(session_id);
    ASSERT_TRUE(info.is_ok());
    EXPECT_EQ(info.value().state, dfs::sync::SessionState::Complete);

    auto stored = store.get("docs/note.txt");
    ASSERT_TRUE(stored.is_ok());
    EXPECT_EQ(stored.value().hash, local_meta.hash);
    EXPECT_EQ(stored.value().size, content.size());
}
