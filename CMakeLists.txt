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

# Example: Add Boost (uncomment if needed)
# Option 1: Use system-installed Boost (faster):
# find_package(Boost 1.70 REQUIRED COMPONENTS system filesystem thread)

# Option 2: Download specific Boost module (much smaller):
# FetchContent_Declare(
#     boost_asio
#     GIT_REPOSITORY https://github.com/boostorg/asio.git
#     GIT_TAG boost-1.82.0
# )
# FetchContent_MakeAvailable(boost_asio)

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