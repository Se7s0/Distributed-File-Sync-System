cmake_minimum_required(VERSION 3.20)
project(DistributedFileSync VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Global compile options
if(MSVC)
    add_compile_options(/W4)  # Warning level 4, but don't treat as errors for now
else()
    add_compile_options(-Wall -Wextra -Werror -pthread)
endif()

# Options
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_EXAMPLES "Build example applications" ON)

# Dependencies
include(FetchContent)

# Google Test for unit testing
if(BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
    enable_testing()
endif()

# spdlog for logging
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.13.0
)
FetchContent_MakeAvailable(spdlog)

# nlohmann/json for JSON handling
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# Boost.Asio for event-driven HTTP server (Phase 2)
# Install via: vcpkg install boost-asio:x64-windows boost-system:x64-windows
find_package(Boost COMPONENTS system)

if(Boost_FOUND)
    message(STATUS "Boost found: ${Boost_VERSION}")
    message(STATUS "Boost include: ${Boost_INCLUDE_DIRS}")
else()
    message(STATUS "Boost not found - Asio server will not be built")
    message(STATUS "To install: vcpkg install boost-asio:x64-windows")
endif()

# Project structure
add_subdirectory(src/core)
add_subdirectory(src/network)
add_subdirectory(src/metadata)
add_subdirectory(src/events)
add_subdirectory(src/sync)
add_subdirectory(src/server)
add_subdirectory(src/client)

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()