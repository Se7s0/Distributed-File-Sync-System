#pragma once

#include "dfs/core/result.hpp"
#include "dfs/metadata/types.hpp"
#include "dfs/sync/types.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace dfs::sync {

/**
 * @brief Discovered changes for a given filesystem scan
 */
struct FileChange {
    enum class Kind {
        Added,
        Modified,
        Deleted
    };

    Kind kind = Kind::Added;
    std::string path;                                        ///< Path relative to scan root (POSIX style)
    metadata::FileMetadata current_metadata;                 ///< Metadata after applying change (tombstone for deletions)
    std::optional<metadata::FileMetadata> previous_metadata; ///< Metadata before change (if any)
    std::uint32_t base_version = 0;                          ///< Version client started editing from
    std::string base_hash;                                   ///< Hash client started editing from
};

struct ChangeSet {
    std::vector<FileChange> changes;
    std::vector<metadata::FileMetadata> snapshot; ///< Latest local snapshot for persistence
};

/**
 * @brief Scans a workspace and produces file change events for Phase 4 sync
 */
class ChangeDetector {
public:
    explicit ChangeDetector(std::string replica_id, bool recursive = true);

    /**
     * @brief Load previously persisted snapshot (e.g., from last sync)
     */
    void load_snapshot(const std::vector<metadata::FileMetadata>& snapshot);

    /**
     * @brief Return the internal cache of known files
     */
    const std::unordered_map<std::string, metadata::FileMetadata>& known_files() const noexcept;

    /**
     * @brief Scan directory and update internal snapshot
     */
    ChangeSet scan_directory(const std::filesystem::path& root);

    /**
     * @brief Replica identifier associated with this detector (device id)
     */
    const std::string& replica_id() const noexcept { return replica_id_; }

private:
    metadata::FileMetadata build_metadata(const std::filesystem::path& absolute_path,
                                          const std::string& relative_path) const;

    static bool metadata_equal(const metadata::FileMetadata& lhs,
                               const metadata::FileMetadata& rhs);

    static std::optional<metadata::ReplicaInfo> find_replica(const metadata::FileMetadata& metadata,
                                                             const std::string& replica_id);

    std::string compute_file_hash(const std::filesystem::path& absolute_path) const;

    std::string replica_id_;
    bool recursive_ = true;
    std::unordered_map<std::string, metadata::FileMetadata> known_;
    std::unordered_map<std::string, std::uint32_t> local_versions_;
};

} // namespace dfs::sync
