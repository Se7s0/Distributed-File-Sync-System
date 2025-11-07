add_executable(socket_example socket_example.cpp)
target_link_libraries(socket_example PRIVATE dfs_network)

add_executable(http_server_example http_server_example.cpp)
target_link_libraries(http_server_example PRIVATE dfs_network)

# New: Comprehensive example demonstrating all three server types
add_executable(http_server_comparison http_server_comparison.cpp)
target_link_libraries(http_server_comparison PRIVATE dfs_network)

# Phase 1.5: Router example (organized routing for Phase 2+)
add_executable(http_router_example http_router_example.cpp)
target_link_libraries(http_router_example PRIVATE dfs_network nlohmann_json::nlohmann_json)

# Phase 2: Metadata server example (complete integration)
add_executable(metadata_server_example metadata_server_example.cpp)
target_link_libraries(metadata_server_example PRIVATE
    dfs_network
    dfs_metadata
    nlohmann_json::nlohmann_json
)

# Phase 2: Metadata server with Boost.Asio (event-driven version)
add_executable(metadata_server_asio_example metadata_server_asio_example.cpp)
target_link_libraries(metadata_server_asio_example PRIVATE
    dfs_network
    dfs_metadata
    nlohmann_json::nlohmann_json
)

# Phase 3: Event-driven metadata server (EventBus pattern)
add_executable(metadata_server_events_example metadata_server_events_example.cpp)
target_link_libraries(metadata_server_events_example PRIVATE
    dfs_network
    dfs_metadata
    dfs_events
    nlohmann_json::nlohmann_json
)

add_executable(sync_demo_server sync_demo_server.cpp)
target_link_libraries(sync_demo_server PRIVATE
    dfs_sync_server
    dfs_sync
    dfs_events
    dfs_metadata
    dfs_network
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)
