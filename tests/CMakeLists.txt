# Phase 3/4 tests

# Include GoogleTest CMake helpers
include(GoogleTest)

# Event Bus tests
add_executable(event_bus_test events/event_bus_test.cpp)
target_link_libraries(event_bus_test PRIVATE
    dfs_events
    GTest::gtest_main
)
gtest_discover_tests(event_bus_test)

# Event Queue tests
add_executable(event_queue_test events/event_queue_test.cpp)
target_link_libraries(event_queue_test PRIVATE
    dfs_events
    GTest::gtest_main
)
gtest_discover_tests(event_queue_test)

add_executable(metrics_component_test events/metrics_component_test.cpp)
target_link_libraries(metrics_component_test PRIVATE
    dfs_events
    GTest::gtest_main
)
gtest_discover_tests(metrics_component_test)

# Change detector tests
add_executable(change_detector_test sync/change_detector_test.cpp)
target_link_libraries(change_detector_test PRIVATE
    dfs_sync
    GTest::gtest_main
)
gtest_discover_tests(change_detector_test)

# Merkle tree tests
add_executable(merkle_tree_test sync/merkle_tree_test.cpp)
target_link_libraries(merkle_tree_test PRIVATE
    dfs_sync
    GTest::gtest_main
)
gtest_discover_tests(merkle_tree_test)

# Sync session tests
add_executable(sync_session_test sync/session_test.cpp)
target_link_libraries(sync_session_test PRIVATE
    dfs_sync
    GTest::gtest_main
)
gtest_discover_tests(sync_session_test)

# Transfer service tests
add_executable(file_transfer_test sync/transfer_test.cpp)
target_link_libraries(file_transfer_test PRIVATE
    dfs_sync
    GTest::gtest_main
)
gtest_discover_tests(file_transfer_test)

# Conflict resolver tests
add_executable(conflict_resolver_test sync/conflict_resolver_test.cpp)
target_link_libraries(conflict_resolver_test PRIVATE
    dfs_sync
    GTest::gtest_main
)
gtest_discover_tests(conflict_resolver_test)

add_executable(sync_service_test sync/sync_service_test.cpp)
target_link_libraries(sync_service_test PRIVATE
    dfs_sync_server
    dfs_sync
    dfs_events
    dfs_metadata
    GTest::gtest_main
)
gtest_discover_tests(sync_service_test)
