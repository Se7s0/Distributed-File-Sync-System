#include "dfs/sync/conflict.hpp"

#include <string>

namespace dfs::sync {
namespace {

const metadata::FileMetadata& select_newest(const metadata::FileMetadata& a,
                                            const metadata::FileMetadata& b) {
    if (a.modified_time == b.modified_time) {
        return (a.hash >= b.hash) ? a : b;
    }
    return (a.modified_time > b.modified_time) ? a : b;
}

} // namespace

dfs::Result<ConflictResolutionResult> ConflictResolver::resolve(const metadata::FileMetadata& local,
                                                                 const metadata::FileMetadata& remote,
                                                                 events::ConflictResolutionStrategy strategy) const {
    if (strategy == events::ConflictResolutionStrategy::LastWriteWins) {
        const auto& winner = select_newest(local, remote);
        const auto& loser = (&winner == &local) ? remote : local;
        ConflictResolutionResult result{winner, loser, strategy, false};
        return dfs::Ok(result);
    }

    if (strategy == events::ConflictResolutionStrategy::Manual) {
        return dfs::Err<ConflictResolutionResult>(std::string("Manual resolution required"));
    }

    if (strategy == events::ConflictResolutionStrategy::Merge) {
        return dfs::Err<ConflictResolutionResult>(std::string("Merge strategy not implemented"));
    }

    return dfs::Err<ConflictResolutionResult>(std::string("Unknown conflict resolution strategy"));
}

} // namespace dfs::sync
