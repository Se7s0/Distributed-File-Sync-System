#pragma once

#include "dfs/core/result.hpp"
#include "dfs/sync/types.hpp"

#include <chrono>
#include <string>

namespace dfs::sync {

class SyncSession {
public:
    SyncSession(std::string session_id, std::string client_id);

    [[nodiscard]] const std::string& session_id() const noexcept { return info_.session_id; }
    [[nodiscard]] const std::string& client_id() const noexcept { return info_.client_id; }
    [[nodiscard]] SessionState state() const noexcept { return info_.state; }
    [[nodiscard]] const SyncSessionInfo& info() const noexcept { return info_; }

    dfs::Result<void> start(std::size_t files_pending, std::size_t bytes_pending);
    dfs::Result<void> transition_to(SessionState next_state);
    dfs::Result<void> mark_failed(std::string error_message);

    void update_pending(std::size_t files_pending, std::size_t bytes_pending);

    [[nodiscard]] std::chrono::system_clock::time_point last_transition() const noexcept {
        return last_transition_;
    }

private:
    [[nodiscard]] bool can_transition(SessionState target) const noexcept;

    SyncSessionInfo info_;
    std::chrono::system_clock::time_point last_transition_{};
};

} // namespace dfs::sync
