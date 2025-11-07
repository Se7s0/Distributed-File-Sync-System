#include "dfs/sync/session.hpp"

#include <gtest/gtest.h>

using dfs::sync::SessionState;
using dfs::sync::SyncSession;

TEST(SyncSessionTest, StartInitialisesSession) {
    SyncSession session{"session-1", "client-A"};

    auto result = session.start(3, 1024);
    ASSERT_TRUE(result.is_ok());

    const auto& info = session.info();
    EXPECT_EQ(info.session_id, "session-1");
    EXPECT_EQ(info.client_id, "client-A");
    EXPECT_EQ(info.state, SessionState::ComputingDiff);
    EXPECT_EQ(info.files_pending, 3u);
    EXPECT_EQ(info.bytes_pending, 1024u);
}

TEST(SyncSessionTest, EnforcesTransitionOrder) {
    SyncSession session{"session-1", "client"};
    ASSERT_TRUE(session.start(0, 0).is_ok());

    EXPECT_TRUE(session.transition_to(SessionState::RequestingMetadata).is_ok());
    EXPECT_TRUE(session.transition_to(SessionState::TransferringFiles).is_ok());
    EXPECT_TRUE(session.transition_to(SessionState::ApplyingChanges).is_ok());
    EXPECT_TRUE(session.transition_to(SessionState::Complete).is_ok());

    auto illegal = session.transition_to(SessionState::TransferringFiles);
    EXPECT_TRUE(illegal.is_error());
}

TEST(SyncSessionTest, AllowsFailureFromAnyState) {
    SyncSession session{"session-1", "client"};
    ASSERT_TRUE(session.start(1, 1024).is_ok());
    ASSERT_TRUE(session.transition_to(SessionState::RequestingMetadata).is_ok());

    auto failed = session.mark_failed("Network error");
    ASSERT_TRUE(failed.is_ok());
    EXPECT_EQ(session.state(), SessionState::Failed);
    EXPECT_EQ(session.info().last_error, "Network error");

    // Further transitions should fail except reapplying failed state
    auto retry = session.transition_to(SessionState::Failed);
    EXPECT_TRUE(retry.is_ok());
    auto illegal = session.transition_to(SessionState::ComputingDiff);
    EXPECT_TRUE(illegal.is_error());
}
