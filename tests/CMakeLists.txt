# Phase 3: Event system tests

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