add_library(dfs_network STATIC
    socket.cpp
    http_server.cpp
)

target_include_directories(dfs_network PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(dfs_network PUBLIC
    dfs_core
    spdlog::spdlog
)

if(WIN32)
    target_link_libraries(dfs_network PUBLIC ws2_32)
endif()