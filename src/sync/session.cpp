#include "dfs/sync/session.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace dfs::sync {
namespace {

bool is_progressive(SessionState current, SessionState target) {
    static const std::unordered_map<SessionState, std::vector<SessionState>> transitions {
        {SessionState::Idle, {SessionState::ComputingDiff}},
        {SessionState::ComputingDiff, {SessionState::RequestingMetadata}},
        {SessionState::RequestingMetadata, {SessionState::TransferringFiles}},
        {SessionState::TransferringFiles, {SessionState::ResolvingConflicts, SessionState::ApplyingChanges, SessionState::Complete}},
        {SessionState::ResolvingConflicts, {SessionState::ApplyingChanges, SessionState::Complete}},
        {SessionState::ApplyingChanges, {SessionState::Complete}},
    };

    if (target == SessionState::Failed) {
        return true;
    }

    const auto it = transitions.find(current);
    if (it == transitions.end()) {
        return false;
    }
    const auto& allowed_list = it->second;
    return std::find(allowed_list.begin(), allowed_list.end(), target) != allowed_list.end();
}

} // namespace

SyncSession::SyncSession(std::string session_id, std::string client_id) {
    info_.session_id = std::move(session_id);
    info_.client_id = std::move(client_id);
    info_.state = SessionState::Idle;
    last_transition_ = std::chrono::system_clock::now();
}

dfs::Result<void> SyncSession::start(std::size_t files_pending, std::size_t bytes_pending) {
    if (info_.state != SessionState::Idle) {
        return dfs::Err<void>(std::string("Session already started"));
    }
    info_.started_at = std::chrono::system_clock::now();
    info_.files_pending = files_pending;
    info_.bytes_pending = bytes_pending;
    return transition_to(SessionState::ComputingDiff);
}

dfs::Result<void> SyncSession::transition_to(SessionState next_state) {
    if (info_.state == next_state) {
        return dfs::Ok();
    }

    if (!can_transition(next_state)) {
        return dfs::Err<void>(std::string("Illegal session state transition"));
    }

    info_.state = next_state;
    last_transition_ = std::chrono::system_clock::now();
    if (next_state != SessionState::Failed) {
        info_.last_error.clear();
    }
    return dfs::Ok();
}

dfs::Result<void> SyncSession::mark_failed(std::string error_message) {
    info_.last_error = std::move(error_message);
    return transition_to(SessionState::Failed);
}

void SyncSession::update_pending(std::size_t files_pending, std::size_t bytes_pending) {
    info_.files_pending = files_pending;
    info_.bytes_pending = bytes_pending;
}

bool SyncSession::can_transition(SessionState target) const noexcept {
    if (info_.state == target) {
        return true;
    }

    if (info_.state == SessionState::Failed || info_.state == SessionState::Complete) {
        return false;
    }

    return is_progressive(info_.state, target);
}

} // namespace dfs::sync
