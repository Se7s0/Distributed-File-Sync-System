# Base sources (always built)
set(NETWORK_SOURCES
    socket.cpp
    http_server.cpp              # Phase 1: Thread pool version
    http_server_legacy.cpp       # Legacy: Single-threaded version
)

# Add Asio server if Boost is available AND file exists
if(Boost_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/http_server_asio.cpp)
    list(APPEND NETWORK_SOURCES http_server_asio.cpp)
    message(STATUS "✓ Building with Boost.Asio event-driven server")
elseif(Boost_FOUND)
    message(STATUS "Boost found, but http_server_asio.cpp not created yet")
    message(STATUS "Create src/network/http_server_asio.cpp to enable Asio server")
endif()

message(STATUS "Building network library with:")
message(STATUS "  - HttpServerLegacy (single-threaded)")
message(STATUS "  - HttpServer (thread pool)")
if(Boost_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/http_server_asio.cpp)
    message(STATUS "  - HttpServerAsio (event-driven)")
endif()

add_library(dfs_network STATIC
    ${NETWORK_SOURCES}
)

target_include_directories(dfs_network PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(dfs_network PUBLIC
    dfs_core
    spdlog::spdlog
)

# Link Boost.Asio if available
if(Boost_FOUND)
    target_link_libraries(dfs_network PUBLIC Boost::system)
    target_compile_definitions(dfs_network PUBLIC DFS_HAS_BOOST_ASIO)
endif()

if(WIN32)
    target_link_libraries(dfs_network PUBLIC ws2_32)
    # Boost.Asio needs Windows version defined
    if(MSVC AND Boost_FOUND)
        target_compile_definitions(dfs_network PUBLIC _WIN32_WINNT=0x0A00)
    endif()
endif()