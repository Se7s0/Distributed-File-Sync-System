# Events module - Event-driven architecture
# Phase 3: Provides EventBus, EventQueue, and Components

# Header-only library (all templates)
add_library(dfs_events INTERFACE)

target_include_directories(dfs_events INTERFACE
    ${CMAKE_SOURCE_DIR}/include
)

# Link dependencies
target_link_libraries(dfs_events INTERFACE
    dfs_core
    dfs_metadata
    spdlog::spdlog
)

# Require C++20
target_compile_features(dfs_events INTERFACE cxx_std_20)
