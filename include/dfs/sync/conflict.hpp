#pragma once

#include "dfs/core/result.hpp"
#include "dfs/metadata/types.hpp"
#include "dfs/events/events.hpp"

namespace dfs::sync {

struct ConflictResolutionResult {
    metadata::FileMetadata resolved;
    metadata::FileMetadata other;
    events::ConflictResolutionStrategy strategy;
    bool requires_manual_attention = false;
};

class ConflictResolver {
public:
    dfs::Result<ConflictResolutionResult> resolve(const metadata::FileMetadata& local,
                                                  const metadata::FileMetadata& remote,
                                                  events::ConflictResolutionStrategy strategy) const;
};

} // namespace dfs::sync
