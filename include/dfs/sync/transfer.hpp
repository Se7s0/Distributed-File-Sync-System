#pragma once

#include "dfs/core/result.hpp"
#include "dfs/sync/types.hpp"

#include <filesystem>
#include <functional>

namespace dfs::sync {

class FileTransferService {
public:
    static constexpr std::size_t kDefaultChunkSize = 64 * 1024;

    dfs::Result<void> upload_file(const std::filesystem::path& source,
                                  const std::string& session_id,
                                  const std::string& logical_path,
                                  const std::function<dfs::Result<void>(ChunkEnvelope&&)>& sink,
                                  std::size_t chunk_size = kDefaultChunkSize) const;

    dfs::Result<void> apply_chunk(const ChunkEnvelope& chunk,
                                  const std::filesystem::path& staging_root) const;

    dfs::Result<void> finalize_file(const std::string& session_id,
                                    const std::string& file_path,
                                    const std::filesystem::path& staging_root,
                                    const std::filesystem::path& destination_root,
                                    const std::string& expected_hash) const;

private:
    static std::filesystem::path make_staging_path(const std::filesystem::path& staging_root,
                                                   const std::string& session_id,
                                                   const std::string& file_path);

    static dfs::Result<void> ensure_parent_exists(const std::filesystem::path& path);
};

} // namespace dfs::sync
