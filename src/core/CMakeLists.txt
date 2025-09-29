add_library(dfs_core INTERFACE)
target_include_directories(dfs_core INTERFACE 
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(dfs_core INTERFACE spdlog::spdlog)