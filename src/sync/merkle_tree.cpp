#include "dfs/sync/merkle_tree.hpp"

#include <iomanip>
#include <iterator>
#include <set>
#include <sstream>

namespace dfs::sync {
namespace {

std::string combine_hashes(const std::map<std::string, std::string>& leaves) {
    if (leaves.empty()) {
        return {};
    }

    std::ostringstream aggregate;
    for (const auto& [path, hash] : leaves) {
        aggregate << path << ':' << hash << ';';
    }
    const auto combined = aggregate.str();
    const std::size_t raw_hash = std::hash<std::string>{}(combined);

    std::ostringstream oss;
    oss << std::hex << std::setw(sizeof(raw_hash) * 2) << std::setfill('0') << raw_hash;
    return oss.str();
}

} // namespace

void MerkleTree::build(const std::vector<metadata::FileMetadata>& files) {
    leaves_.clear();
    for (const auto& metadata : files) {
        leaves_[metadata.file_path] = hash_leaf(metadata);
    }
    recompute_root();
}

std::vector<std::string> MerkleTree::diff(const MerkleTree& other) const {
    std::vector<std::string> differences;

    auto it_a = leaves_.begin();
    auto it_b = other.leaves_.begin();

    while (it_a != leaves_.end() || it_b != other.leaves_.end()) {
        if (it_b == other.leaves_.end() || (it_a != leaves_.end() && it_a->first < it_b->first)) {
            differences.push_back(it_a->first);
            ++it_a;
            continue;
        }

        if (it_a == leaves_.end() || it_b->first < it_a->first) {
            differences.push_back(it_b->first);
            ++it_b;
            continue;
        }

        if (it_a->second != it_b->second) {
            differences.push_back(it_a->first);
        }
        ++it_a;
        ++it_b;
    }

    return differences;
}

std::string MerkleTree::hash_leaf(const metadata::FileMetadata& metadata) {
    const std::string payload = metadata.file_path + '|' + metadata.hash + '|' + std::to_string(metadata.size);
    const std::size_t raw_hash = std::hash<std::string>{}(payload);
    return hash_to_hex(raw_hash);
}

std::string MerkleTree::hash_to_hex(std::size_t value) {
    std::ostringstream oss;
    oss << std::hex << std::setw(sizeof(value) * 2) << std::setfill('0') << value;
    return oss.str();
}

void MerkleTree::recompute_root() {
    root_hash_ = combine_hashes(leaves_);
}

} // namespace dfs::sync
