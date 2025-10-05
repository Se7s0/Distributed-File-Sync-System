#pragma once

/**
 * @file serializer.hpp
 * @brief Binary serialization for file metadata (network transmission)
 *
 * WHY THIS FILE EXISTS:
 * We need to send FileMetadata over the network efficiently.
 * Binary format is much more efficient than text (DDL or JSON).
 *
 * EFFICIENCY COMPARISON (for typical file metadata):
 * - DDL text:   ~200-300 bytes (human-readable, verbose)
 * - JSON:       ~150-250 bytes (structured but verbose)
 * - Binary:     ~80-120 bytes (compact, fast to parse)
 *
 * EXAMPLE:
 * File metadata for "/test.txt":
 * - DDL: 'FILE "/test.txt" HASH "abc123..." SIZE 1024 ...' (250 bytes)
 * - Binary: [binary blob] (90 bytes) - 2.7x smaller!
 *
 * WHY BINARY SERIALIZATION:
 * 1. Bandwidth: 2-3x smaller than text formats
 * 2. Speed: No string parsing, just memcpy
 * 3. Network efficiency: Less data to transfer
 * 4. Battery: Less CPU for mobile devices
 *
 * WHEN TO USE:
 * - Network transmission: Client ↔ Server communication
 * - Disk persistence: Save metadata to file
 * - IPC: Inter-process communication
 *
 * WHEN NOT TO USE:
 * - Debugging: Binary is not human-readable (use DDL)
 * - Configuration: Users can't edit binary (use DDL)
 * - Logging: Can't read in text editor (use DDL)
 *
 * HOW IT INTEGRATES:
 * HTTP API flow:
 * 1. Client has FileMetadata struct
 * 2. Serializer converts to binary (std::vector<uint8_t>)
 * 3. Send binary over HTTP
 * 4. Server receives binary
 * 5. Serializer converts back to FileMetadata struct
 * 6. Store saves FileMetadata
 *
 * DESIGN DECISIONS:
 * - Simple format: length-prefixed strings, fixed-size integers
 * - Little-endian: Standard for x86/ARM processors
 * - Version byte: For future format changes
 * - No compression: Keep it simple for Phase 2 (add in Phase 3+)
 *
 * BINARY FORMAT:
 * [VERSION: 1 byte]
 * [file_path_length: 4 bytes] [file_path: N bytes]
 * [hash_length: 4 bytes] [hash: N bytes]
 * [size: 8 bytes]
 * [modified_time: 8 bytes]
 * [created_time: 8 bytes]
 * [sync_state: 1 byte]
 * [replica_count: 4 bytes]
 * For each replica:
 *   [replica_id_length: 4 bytes] [replica_id: N bytes]
 *   [version: 4 bytes]
 *   [modified_time: 8 bytes]
 */

#include "dfs/metadata/types.hpp"
#include "dfs/core/result.hpp"
#include <vector>
#include <cstdint>
#include <cstring>

// Cross-platform network byte order conversion
#ifdef _WIN32
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <arpa/inet.h>
#endif

namespace dfs {
namespace metadata {

/**
 * @brief Binary serializer for FileMetadata
 *
 * WHY THIS CLASS:
 * Provides serialize() and deserialize() methods for converting
 * FileMetadata to/from binary format for network transmission.
 *
 * DESIGN PATTERN:
 * This is a utility class with static methods (stateless).
 * No need to create instances - just call Serializer::serialize().
 */
class Serializer {
public:
    /**
     * Serialize FileMetadata to binary format
     *
     * WHY THIS METHOD:
     * Convert in-memory struct to byte array for network/disk.
     *
     * HOW IT WORKS:
     * 1. Create empty byte vector
     * 2. Append version byte
     * 3. Append each field in order:
     *    - Strings: [4-byte length] [string bytes]
     *    - Integers: [8 bytes or 4 bytes depending on type]
     *    - Enums: [1 byte]
     * 4. Append replica count and all replicas
     * 5. Return byte vector
     *
     * EXAMPLE:
     * FileMetadata metadata;
     * metadata.file_path = "/test.txt";
     * metadata.hash = "abc123";
     * metadata.size = 1024;
     *
     * auto binary = Serializer::serialize(metadata);
     * // binary is now std::vector<uint8_t> ready to send over network
     *
     * @param metadata FileMetadata to serialize
     * @return Binary representation as byte vector
     */
    static std::vector<uint8_t> serialize(const FileMetadata& metadata) {
        std::vector<uint8_t> buffer;

        // Version byte (for future format changes)
        // If we change format in Phase 3, we can bump this to version 2
        // and handle both formats in deserialize()
        write_uint8(buffer, 1);  // Version 1

        // File path (length-prefixed string)
        write_string(buffer, metadata.file_path);

        // Hash (length-prefixed string)
        write_string(buffer, metadata.hash);

        // Size (8 bytes)
        write_uint64(buffer, metadata.size);

        // Modified time (8 bytes)
        write_int64(buffer, metadata.modified_time);

        // Created time (8 bytes)
        write_int64(buffer, metadata.created_time);

        // Sync state (1 byte)
        write_uint8(buffer, static_cast<uint8_t>(metadata.sync_state));

        // Replica count (4 bytes)
        write_uint32(buffer, static_cast<uint32_t>(metadata.replicas.size()));

        // Each replica
        for (const auto& replica : metadata.replicas) {
            write_string(buffer, replica.replica_id);
            write_uint32(buffer, replica.version);
            write_int64(buffer, replica.modified_time);
        }

        return buffer;
    }

    /**
     * Deserialize binary format to FileMetadata
     *
     * WHY THIS METHOD:
     * Convert byte array from network/disk back to in-memory struct.
     *
     * HOW IT WORKS:
     * 1. Create cursor at position 0
     * 2. Read version byte and check it's valid
     * 3. Read each field in same order as serialize():
     *    - Strings: Read 4-byte length, then read that many bytes
     *    - Integers: Read 8 or 4 bytes
     *    - Enums: Read 1 byte
     * 4. Read replica count and all replicas
     * 5. Return FileMetadata struct
     *
     * ERROR HANDLING:
     * If binary data is corrupt or too short, return error.
     * This prevents crashes from malformed network data.
     *
     * EXAMPLE:
     * std::vector<uint8_t> binary = receive_from_network();
     * auto result = Serializer::deserialize(binary);
     * if (result.is_ok()) {
     *     FileMetadata metadata = result.value();
     *     // Use metadata...
     * } else {
     *     // Corrupt data!
     *     std::cerr << result.error() << std::endl;
     * }
     *
     * @param data Binary data to deserialize
     * @return Result<FileMetadata> - metadata if valid, error if corrupt
     */
    static Result<FileMetadata> deserialize(const std::vector<uint8_t>& data) {
        size_t cursor = 0;
        FileMetadata metadata;

        // Read version byte
        auto version_result = read_uint8(data, cursor);
        if (version_result.is_error()) {
            return Err<FileMetadata, std::string>(version_result.error());
        }
        uint8_t version = version_result.value();

        // Check version
        if (version != 1) {
            return Err<FileMetadata, std::string>(
                "Unsupported serialization version: " + std::to_string(version)
            );
        }

        // Read file path
        auto path_result = read_string(data, cursor);
        if (path_result.is_error()) {
            return Err<FileMetadata, std::string>(path_result.error());
        }
        metadata.file_path = path_result.value();

        // Read hash
        auto hash_result = read_string(data, cursor);
        if (hash_result.is_error()) {
            return Err<FileMetadata, std::string>(hash_result.error());
        }
        metadata.hash = hash_result.value();

        // Read size
        auto size_result = read_uint64(data, cursor);
        if (size_result.is_error()) {
            return Err<FileMetadata, std::string>(size_result.error());
        }
        metadata.size = size_result.value();

        // Read modified time
        auto modified_result = read_int64(data, cursor);
        if (modified_result.is_error()) {
            return Err<FileMetadata, std::string>(modified_result.error());
        }
        metadata.modified_time = modified_result.value();

        // Read created time
        auto created_result = read_int64(data, cursor);
        if (created_result.is_error()) {
            return Err<FileMetadata, std::string>(created_result.error());
        }
        metadata.created_time = created_result.value();

        // Read sync state
        auto state_result = read_uint8(data, cursor);
        if (state_result.is_error()) {
            return Err<FileMetadata, std::string>(state_result.error());
        }
        metadata.sync_state = static_cast<SyncState>(state_result.value());

        // Read replica count
        auto count_result = read_uint32(data, cursor);
        if (count_result.is_error()) {
            return Err<FileMetadata, std::string>(count_result.error());
        }
        uint32_t replica_count = count_result.value();

        // Read each replica
        for (uint32_t i = 0; i < replica_count; ++i) {
            ReplicaInfo replica;

            // Read replica ID
            auto id_result = read_string(data, cursor);
            if (id_result.is_error()) {
                return Err<FileMetadata, std::string>(id_result.error());
            }
            replica.replica_id = id_result.value();

            // Read version
            auto ver_result = read_uint32(data, cursor);
            if (ver_result.is_error()) {
                return Err<FileMetadata, std::string>(ver_result.error());
            }
            replica.version = ver_result.value();

            // Read modified time
            auto time_result = read_int64(data, cursor);
            if (time_result.is_error()) {
                return Err<FileMetadata, std::string>(time_result.error());
            }
            replica.modified_time = time_result.value();

            metadata.replicas.push_back(replica);
        }

        return Ok(metadata);
    }

private:
    /**
     * Helper functions for writing primitive types to buffer
     *
     * WHY THESE HELPERS:
     * - Encapsulate byte-order conversion (host → network byte order)
     * - Reduce code duplication
     * - Make serialize() more readable
     *
     * BYTE ORDER:
     * Different CPUs store multi-byte integers differently:
     * - Little-endian (x86, ARM): 0x1234 stored as [34 12]
     * - Big-endian (network): 0x1234 stored as [12 34]
     *
     * We use network byte order (big-endian) for compatibility.
     */

    static void write_uint8(std::vector<uint8_t>& buffer, uint8_t value) {
        buffer.push_back(value);
    }

    static void write_uint32(std::vector<uint8_t>& buffer, uint32_t value) {
        // Convert to network byte order (big-endian)
        uint32_t network_value = htonl(value);
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&network_value);
        buffer.insert(buffer.end(), bytes, bytes + 4);
    }

    static void write_uint64(std::vector<uint8_t>& buffer, uint64_t value) {
        // Convert to network byte order
        uint64_t network_value = htonll_cross_platform(value);
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&network_value);
        buffer.insert(buffer.end(), bytes, bytes + 8);
    }

    static void write_int64(std::vector<uint8_t>& buffer, int64_t value) {
        // Cast to unsigned for byte operations
        write_uint64(buffer, static_cast<uint64_t>(value));
    }

    static void write_string(std::vector<uint8_t>& buffer, const std::string& str) {
        // Write length (4 bytes)
        write_uint32(buffer, static_cast<uint32_t>(str.length()));

        // Write string bytes
        buffer.insert(buffer.end(), str.begin(), str.end());
    }

    /**
     * Helper functions for reading primitive types from buffer
     *
     * WHY THESE HELPERS:
     * - Handle bounds checking (prevent reading past end of buffer)
     * - Convert from network byte order back to host byte order
     * - Return Result<T> for error handling
     */

    static Result<uint8_t> read_uint8(const std::vector<uint8_t>& buffer, size_t& cursor) {
        if (cursor + 1 > buffer.size()) {
            return Err<uint8_t, std::string>("Buffer underflow reading uint8");
        }

        uint8_t value = buffer[cursor];
        cursor += 1;
        return Ok(value);
    }

    static Result<uint32_t> read_uint32(const std::vector<uint8_t>& buffer, size_t& cursor) {
        if (cursor + 4 > buffer.size()) {
            return Err<uint32_t, std::string>("Buffer underflow reading uint32");
        }

        uint32_t network_value;
        std::memcpy(&network_value, &buffer[cursor], 4);
        uint32_t value = ntohl(network_value);  // Convert from network byte order

        cursor += 4;
        return Ok(value);
    }

    static Result<uint64_t> read_uint64(const std::vector<uint8_t>& buffer, size_t& cursor) {
        if (cursor + 8 > buffer.size()) {
            return Err<uint64_t, std::string>("Buffer underflow reading uint64");
        }

        uint64_t network_value;
        std::memcpy(&network_value, &buffer[cursor], 8);
        uint64_t value = ntohll_cross_platform(network_value);  // Convert from network byte order

        cursor += 8;
        return Ok(value);
    }

    static Result<int64_t> read_int64(const std::vector<uint8_t>& buffer, size_t& cursor) {
        auto result = read_uint64(buffer, cursor);
        if (result.is_error()) {
            return Err<int64_t, std::string>(result.error());
        }
        return Ok(static_cast<int64_t>(result.value()));
    }

    static Result<std::string> read_string(const std::vector<uint8_t>& buffer, size_t& cursor) {
        // Read length
        auto length_result = read_uint32(buffer, cursor);
        if (length_result.is_error()) {
            return Err<std::string, std::string>(length_result.error());
        }
        uint32_t length = length_result.value();

        // Check bounds
        if (cursor + length > buffer.size()) {
            return Err<std::string, std::string>("Buffer underflow reading string");
        }

        // Read string bytes
        std::string value(buffer.begin() + cursor, buffer.begin() + cursor + length);
        cursor += length;

        return Ok(value);
    }

    /**
     * Byte order conversion helpers
     *
     * WHY: htonll/ntohll don't exist in all C libraries
     * We implement them ourselves for portability
     */
    static uint64_t htonll_cross_platform(uint64_t value) {
        #ifdef _WIN32
            // Windows: manual byte swapping
            return ((value & 0x00000000000000FFULL) << 56) |
                   ((value & 0x000000000000FF00ULL) << 40) |
                   ((value & 0x0000000000FF0000ULL) << 24) |
                   ((value & 0x00000000FF000000ULL) << 8)  |
                   ((value & 0x000000FF00000000ULL) >> 8)  |
                   ((value & 0x0000FF0000000000ULL) >> 24) |
                   ((value & 0x00FF000000000000ULL) >> 40) |
                   ((value & 0xFF00000000000000ULL) >> 56);
        #else
            // Unix/Linux: use compiler builtin or macro
            #if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
                return __builtin_bswap64(value);
            #else
                return value;  // Already big-endian
            #endif
        #endif
    }

    static uint64_t ntohll_cross_platform(uint64_t value) {
        // Same operation as htonll (conversion is symmetric)
        return htonll_cross_platform(value);
    }
};

} // namespace metadata
} // namespace dfs
