add_library(dfs_sync_server
    sync_service.cpp
)

target_include_directories(dfs_sync_server
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(dfs_sync_server
    PUBLIC
        dfs_sync
        dfs_events
        dfs_metadata
        dfs_core
        spdlog::spdlog
)

