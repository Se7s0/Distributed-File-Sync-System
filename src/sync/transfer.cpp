#include "dfs/sync/transfer.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace dfs::sync {
namespace fs = std::filesystem;

namespace {

std::string hash_vector(const std::vector<std::uint8_t>& data) {
    const std::uint64_t offset = 0xcbf29ce484222325ULL;
    const std::uint64_t prime  = 0x100000001b3ULL;
    std::uint64_t hash = offset;
    for (std::uint8_t byte : data) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= prime;
    }
    std::ostringstream oss;
    oss << std::hex << std::setw(sizeof(hash) * 2) << std::setfill('0') << hash;
    return oss.str();
}

std::string hash_stream(std::ifstream& stream) {
    const std::uint64_t offset = 0xcbf29ce484222325ULL;
    const std::uint64_t prime  = 0x100000001b3ULL;
    std::uint64_t hash = offset;
    char buffer[4096];
    while (stream.read(buffer, sizeof(buffer)) || stream.gcount() > 0) {
        const std::streamsize count = stream.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(buffer[i]));
            hash *= prime;
        }
    }
    std::ostringstream hex;
    hex << std::hex << std::setw(sizeof(hash) * 2) << std::setfill('0') << hash;
    return hex.str();
}

} // namespace

dfs::Result<void> FileTransferService::upload_file(const fs::path& source,
                                                    const std::string& session_id,
                                                    const std::string& logical_path,
                                                    const std::function<dfs::Result<void>(ChunkEnvelope&&)>& sink,
                                                    std::size_t chunk_size) const {
    if (chunk_size == 0) {
        return dfs::Err<void>(std::string("chunk_size must be > 0"));
    }

    std::ifstream input(source, std::ios::binary);
    if (!input) {
        return dfs::Err<void>(std::string("Failed to open source file: ") + source.string());
    }

    const auto file_size = fs::file_size(source);
    const std::uint32_t total_chunks = static_cast<std::uint32_t>((file_size + chunk_size - 1) / chunk_size);

    std::vector<std::uint8_t> buffer(chunk_size);
    std::uint32_t chunk_index = 0;

    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(chunk_size));
        const auto bytes_read = static_cast<std::size_t>(input.gcount());
        if (bytes_read == 0) {
            break;
        }

        buffer.resize(bytes_read);
        ChunkEnvelope envelope;
        envelope.session_id = session_id;
        envelope.file_path = logical_path;
        envelope.chunk_index = chunk_index;
        envelope.total_chunks = total_chunks;
        envelope.chunk_size = static_cast<std::uint32_t>(chunk_size);
        envelope.data = buffer;
        envelope.chunk_hash = hash_vector(envelope.data);

        auto result = sink(std::move(envelope));
        if (result.is_error()) {
            return result;
        }

        ++chunk_index;
        buffer.assign(chunk_size, 0);
    }

    return dfs::Ok();
}

dfs::Result<void> FileTransferService::apply_chunk(const ChunkEnvelope& chunk,
                                                    const fs::path& staging_root) const {
    const auto staging_path = make_staging_path(staging_root, chunk.session_id, chunk.file_path);
    if (auto res = ensure_parent_exists(staging_path); res.is_error()) {
        return res;
    }

    if (chunk.chunk_hash != hash_vector(chunk.data)) {
        return dfs::Err<void>(std::string("Chunk hash mismatch for ") + chunk.file_path);
    }

    const auto offset = static_cast<std::streamoff>(chunk.chunk_index) * static_cast<std::streamoff>(chunk.chunk_size);

    std::fstream file(staging_path, std::ios::in | std::ios::out | std::ios::binary);
    if (!file) {
        std::ofstream create(staging_path, std::ios::binary | std::ios::trunc);
        if (!create) {
            return dfs::Err<void>(std::string("Failed to create staging file: ") + staging_path.string());
        }
        create.close();
        file.open(staging_path, std::ios::in | std::ios::out | std::ios::binary);
    }

    if (!file) {
        return dfs::Err<void>(std::string("Failed to open staging file: ") + staging_path.string());
    }

    file.seekp(offset);
    file.write(reinterpret_cast<const char*>(chunk.data.data()), static_cast<std::streamsize>(chunk.data.size()));
    if (!file) {
        return dfs::Err<void>(std::string("Failed to write chunk for ") + chunk.file_path);
    }

    file.flush();
    return dfs::Ok();
}

dfs::Result<void> FileTransferService::finalize_file(const std::string& session_id,
                                                      const std::string& file_path,
                                                      const fs::path& staging_root,
                                                      const fs::path& destination_root,
                                                      const std::string& expected_hash) const {
    const auto staging_path = make_staging_path(staging_root, session_id, file_path);
    if (!fs::exists(staging_path)) {
        return dfs::Err<void>(std::string("Staging file missing: ") + staging_path.string());
    }

    std::ifstream input(staging_path, std::ios::binary);
    if (!input) {
        return dfs::Err<void>(std::string("Failed to open staging file: ") + staging_path.string());
    }

    if (expected_hash != hash_stream(input)) {
        return dfs::Err<void>(std::string("Final hash mismatch for ") + file_path);
    }

    input.close();

    const fs::path destination_path = destination_root / fs::path(file_path).relative_path();
    if (auto res = ensure_parent_exists(destination_path); res.is_error()) {
        return res;
    }

    std::error_code ec;
    fs::rename(staging_path, destination_path, ec);
    if (ec) {
        return dfs::Err<void>(std::string("Failed to move staging file: ") + destination_path.string());
    }

    return dfs::Ok();
}

fs::path FileTransferService::make_staging_path(const fs::path& staging_root,
                                                const std::string& session_id,
                                                const std::string& file_path) {
    fs::path relative = fs::path(file_path).relative_path();
    return staging_root / session_id / relative;
}

dfs::Result<void> FileTransferService::ensure_parent_exists(const fs::path& path) {
    const auto parent = path.parent_path();
    std::error_code ec;
    fs::create_directories(parent, ec);
    if (ec && !fs::exists(parent)) {
        return dfs::Err<void>(std::string("Failed to create directory: ") + parent.string());
    }
    return dfs::Ok();
}

} // namespace dfs::sync
