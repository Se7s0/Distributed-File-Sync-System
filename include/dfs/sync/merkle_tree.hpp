#pragma once

#include "dfs/metadata/types.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace dfs::sync {

class MerkleTree {
public:
    void build(const std::vector<metadata::FileMetadata>& files);

    [[nodiscard]] std::vector<std::string> diff(const MerkleTree& other) const;

    [[nodiscard]] const std::string& root_hash() const noexcept { return root_hash_; }

    [[nodiscard]] bool empty() const noexcept { return leaves_.empty(); }

    [[nodiscard]] const std::map<std::string, std::string>& leaves() const noexcept { return leaves_; }

private:
    static std::string hash_leaf(const metadata::FileMetadata& metadata);

    static std::string hash_to_hex(std::size_t value);

    void recompute_root();

    std::map<std::string, std::string> leaves_; // path -> hash
    std::string root_hash_;
};

} // namespace dfs::sync
