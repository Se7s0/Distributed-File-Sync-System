# Phase 4: Sync module (initial scaffolding)

add_library(dfs_sync
    change_detector.cpp
    merkle_tree.cpp
    session.cpp
    transfer.cpp
    conflict.cpp
)

target_include_directories(dfs_sync
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(dfs_sync
    PUBLIC
        dfs_core
        dfs_metadata
        dfs_events
        spdlog::spdlog
)

target_compile_features(dfs_sync PUBLIC cxx_std_20)
