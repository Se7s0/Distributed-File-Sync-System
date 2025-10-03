add_executable(socket_example socket_example.cpp)
target_link_libraries(socket_example PRIVATE dfs_network)

add_executable(http_server_example http_server_example.cpp)
target_link_libraries(http_server_example PRIVATE dfs_network)