#include "dfs/sync/change_detector.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

using dfs::metadata::FileMetadata;
using dfs::metadata::ReplicaInfo;
using dfs::metadata::SyncState;

namespace fs = std::filesystem;

namespace dfs::sync {
namespace {

std::time_t to_time_t(fs::file_time_type time) {
    using namespace std::chrono;
    const auto system_time = time_point_cast<system_clock::duration>(
        time - fs::file_time_type::clock::now() + system_clock::now());
    return system_clock::to_time_t(system_time);
}

std::string hash_to_hex(std::size_t value) {
    std::ostringstream oss;
    oss << std::hex << std::setw(sizeof(value) * 2) << std::setfill('0') << value;
    return oss.str();
}

} // namespace

ChangeDetector::ChangeDetector(std::string replica_id, bool recursive)
    : replica_id_(std::move(replica_id)), recursive_(recursive) {}

void ChangeDetector::load_snapshot(const std::vector<FileMetadata>& snapshot) {
    known_.clear();
    local_versions_.clear();
    for (const auto& entry : snapshot) {
        known_.emplace(entry.file_path, entry);
        if (auto replica = find_replica(entry, replica_id_)) {
            local_versions_[entry.file_path] = replica->version;
        }
    }
}

const std::unordered_map<std::string, FileMetadata>& ChangeDetector::known_files() const noexcept {
    return known_;
}

ChangeSet ChangeDetector::scan_directory(const fs::path& root) {
    ChangeSet result;

    if (root.empty() || !fs::exists(root) || !fs::is_directory(root)) {
        return result;
    }

    std::unordered_map<std::string, FileMetadata> next_snapshot;

    auto process_entry = [&](const fs::directory_entry& entry) {
        if (!entry.is_regular_file()) {
            return;
        }

        std::error_code ec;
        auto relative = fs::relative(entry.path(), root, ec);
        if (ec || relative.empty()) {
            return;
        }

        std::string normalized = relative.generic_string();
        FileMetadata new_metadata = build_metadata(entry.path(), normalized);

        auto known_it = known_.find(normalized);
        if (known_it == known_.end()) {
            // New file detected
            new_metadata.sync_state = SyncState::MODIFIED;
            new_metadata.update_replica(replica_id_, 1, new_metadata.modified_time);
            local_versions_[normalized] = 1;

            FileChange change;
            change.kind = FileChange::Kind::Added;
            change.path = normalized;
            change.current_metadata = new_metadata;
            change.base_version = 0;
            change.base_hash.clear();

            result.changes.push_back(change);
            next_snapshot.emplace(normalized, new_metadata);
            return;
        }

        const auto& old_metadata = known_it->second;
        if (metadata_equal(old_metadata, new_metadata)) {
            // No change; keep previous metadata (preserves replica info)
            next_snapshot.emplace(normalized, old_metadata);
            if (auto replica = find_replica(old_metadata, replica_id_)) {
                local_versions_[normalized] = replica->version;
            }
            return;
        }

        // Modified file
        std::uint32_t base_version = 0;
        if (auto replica = find_replica(old_metadata, replica_id_)) {
            base_version = replica->version;
        }
        std::uint32_t new_version = base_version + 1;

        FileMetadata updated_metadata = new_metadata;
        updated_metadata.sync_state = SyncState::MODIFIED;
        updated_metadata.replicas = old_metadata.replicas;
        updated_metadata.update_replica(replica_id_, new_version, updated_metadata.modified_time);
        local_versions_[normalized] = new_version;

        FileChange change;
        change.kind = FileChange::Kind::Modified;
        change.path = normalized;
        change.current_metadata = updated_metadata;
        change.previous_metadata = old_metadata;
        change.base_version = base_version;
        change.base_hash = old_metadata.hash;

        result.changes.push_back(change);
        next_snapshot.emplace(normalized, updated_metadata);
    };

    if (recursive_) {
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            process_entry(entry);
        }
    } else {
        for (const auto& entry : fs::directory_iterator(root)) {
            process_entry(entry);
        }
    }

    // Detect deletions
    for (const auto& [path, old_metadata] : known_) {
        if (next_snapshot.find(path) != next_snapshot.end()) {
            continue;
        }

        FileChange change;
        change.kind = FileChange::Kind::Deleted;
        change.path = path;

        FileMetadata tombstone = old_metadata;
        tombstone.sync_state = SyncState::DELETED;
        change.current_metadata = tombstone;
        change.previous_metadata = old_metadata;
        change.base_hash = old_metadata.hash;
        if (auto replica = find_replica(old_metadata, replica_id_)) {
            change.base_version = replica->version;
        }

        result.changes.push_back(change);
        local_versions_.erase(path);
    }

    known_ = next_snapshot;

    result.snapshot.reserve(known_.size());
    for (const auto& [_, metadata] : known_) {
        result.snapshot.push_back(metadata);
    }

    return result;
}

FileMetadata ChangeDetector::build_metadata(const fs::path& absolute_path,
                                            const std::string& relative_path) const {
    FileMetadata metadata;
    metadata.file_path = relative_path;
    metadata.size = fs::file_size(absolute_path);
    metadata.hash = compute_file_hash(absolute_path);

    auto write_time = fs::last_write_time(absolute_path);
    auto timestamp = to_time_t(write_time);
    metadata.modified_time = timestamp;
    metadata.created_time = timestamp;
    metadata.sync_state = SyncState::SYNCED;

    return metadata;
}

bool ChangeDetector::metadata_equal(const FileMetadata& lhs, const FileMetadata& rhs) {
    return lhs.hash == rhs.hash && lhs.size == rhs.size && lhs.modified_time == rhs.modified_time;
}

std::optional<ReplicaInfo> ChangeDetector::find_replica(const FileMetadata& metadata,
                                                        const std::string& replica_id) {
    for (const auto& replica : metadata.replicas) {
        if (replica.replica_id == replica_id) {
            return replica;
        }
    }
    return std::nullopt;
}

std::string ChangeDetector::compute_file_hash(const fs::path& absolute_path) const {
    std::ifstream input(absolute_path, std::ios::binary);
    if (!input) {
        return {};
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string data = buffer.str();

    const std::size_t hash_value = std::hash<std::string>{}(data);
    return hash_to_hex(hash_value);
}

} // namespace dfs::sync
