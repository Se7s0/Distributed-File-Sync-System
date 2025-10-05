# ════════════════════════════════════════════════════════════
# Phase 2: Metadata Module
# ════════════════════════════════════════════════════════════
#
# WHY THIS FILE:
# Builds the metadata library with all Phase 2 components:
# - Types (FileMetadata, ReplicaInfo, SyncState)
# - Lexer (tokenizer for DDL)
# - Parser (DDL → FileMetadata)
# - Store (thread-safe in-memory storage)
# - Serializer (binary serialization)
#
# DESIGN NOTE:
# This is a header-only library for Phase 2 (all implementations in .hpp files).
# We create an INTERFACE library so other targets can link against it.
#
# In Phase 3+, we may add .cpp files if implementations get large.

# Create header-only interface library
# WHY INTERFACE: No .cpp files to compile, just headers to include
add_library(dfs_metadata INTERFACE)

# Specify include directories
# WHY: Other targets that link dfs_metadata get these include paths
target_include_directories(dfs_metadata INTERFACE
    ${PROJECT_SOURCE_DIR}/include
)

# Link dependencies
# WHY: Metadata code uses these libraries
target_link_libraries(dfs_metadata INTERFACE
    dfs_core           # For Result<T> error handling
    spdlog::spdlog     # For logging (used in parser error messages)
)

# Install headers (for system-wide installation)
install(
    DIRECTORY ${PROJECT_SOURCE_DIR}/include/dfs/metadata
    DESTINATION include/dfs
    FILES_MATCHING PATTERN "*.hpp"
)
