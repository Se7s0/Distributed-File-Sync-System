#pragma once

/**
 * @file parser.hpp
 * @brief Parser for metadata DDL (converts tokens to FileMetadata structures)
 *
 * WHY THIS FILE EXISTS:
 * The parser is the second stage of DDL processing:
 * 1. Lexer converts text → tokens
 * 2. Parser converts tokens → FileMetadata structs
 *
 * EXAMPLE FLOW:
 * DDL Text:
 *   FILE "/test.txt" HASH "abc123" SIZE 100
 *
 * After Lexer:
 *   [FILE] [STRING:/test.txt] [HASH] [STRING:abc123] [SIZE] [NUMBER:100]
 *
 * After Parser:
 *   FileMetadata { file_path="/test.txt", hash="abc123", size=100 }
 *
 * WHY RECURSIVE DESCENT PARSING:
 * - Simple to understand and implement
 * - Natural mapping from grammar rules to functions
 * - Good error messages (we know exactly what we expected)
 * - This is how many production parsers work (GCC, Clang, etc.)
 *
 * GRAMMAR:
 * <file_metadata> ::= FILE <string>
 *                     [ HASH <string> ]
 *                     [ SIZE <number> ]
 *                     [ MODIFIED <number> ]
 *                     [ CREATED <number> ]
 *                     [ STATE <sync_state> ]
 *                     [ <replica>* ]
 *
 * <replica> ::= REPLICA <string> VERSION <number> MODIFIED <number>
 *
 * <sync_state> ::= SYNCED | MODIFIED | SYNCING | CONFLICT | DELETED
 *
 * HOW IT INTEGRATES:
 * - HTTP endpoints receive DDL text from clients
 * - Parser converts DDL → FileMetadata
 * - Store saves FileMetadata to memory
 * - Serializer converts FileMetadata → binary for network transmission
 */

#include "dfs/metadata/types.hpp"
#include "dfs/metadata/lexer.hpp"
#include "dfs/core/result.hpp"
#include <vector>
#include <sstream>

namespace dfs {
namespace metadata {

/**
 * @brief Parser for metadata DDL
 *
 * WHY THIS CLASS:
 * Converts token stream from Lexer into FileMetadata structures.
 *
 * HOW IT WORKS (Recursive Descent):
 * Each grammar rule becomes a function:
 * - parse_file_metadata() handles <file_metadata> rule
 * - parse_replica() handles <replica> rule
 * - parse_sync_state() handles <sync_state> rule
 *
 * Each function:
 * 1. Checks if current token matches expected token
 * 2. If yes: consume token and continue
 * 3. If no: return error with helpful message
 *
 * EXAMPLE USAGE:
 * Parser parser("FILE \"/test.txt\" HASH \"abc\" SIZE 100");
 * auto result = parser.parse_file_metadata();
 * if (result.is_ok()) {
 *     FileMetadata metadata = result.value();
 *     // Use metadata...
 * } else {
 *     std::cerr << "Parse error: " << result.error() << std::endl;
 * }
 */
class Parser {
public:
    /**
     * Constructor
     *
     * @param input DDL text to parse
     *
     * WHY: Create lexer and get first token to start parsing
     */
    explicit Parser(const std::string& input)
        : lexer_(input), current_token_(lexer_.next_token()) {}

    /**
     * Parse a complete file metadata definition
     *
     * WHY THIS METHOD:
     * This is the main entry point for parsing. It parses a complete
     * FILE definition with all its attributes.
     *
     * EXPECTED FORMAT:
     * FILE "/path/to/file.txt"
     *   HASH "sha256_hash"
     *   SIZE 1024
     *   MODIFIED 1704096000
     *   CREATED 1704000000
     *   STATE SYNCED
     *   REPLICA "laptop_1" VERSION 5 MODIFIED 1704096000
     *   REPLICA "phone_1" VERSION 4 MODIFIED 1703000000
     *
     * HOW IT WORKS:
     * 1. Expect FILE keyword
     * 2. Expect string (file path)
     * 3. Parse optional attributes (HASH, SIZE, etc.)
     * 4. Parse optional replicas
     * 5. Return FileMetadata struct
     *
     * @return Result<FileMetadata> - success or error message
     */
    Result<FileMetadata> parse_file_metadata() {
        FileMetadata metadata;

        // Expect: FILE
        if (!expect(TokenType::FILE)) {
            return Err<FileMetadata, std::string>(
                error_message("Expected FILE keyword")
            );
        }

        // Expect: <string> (file path)
        if (!expect(TokenType::STRING)) {
            return Err<FileMetadata, std::string>(
                error_message("Expected file path string after FILE")
            );
        }
        metadata.file_path = previous_token_.lexeme;

        // Parse optional attributes
        while (!is_at_end()) {
            TokenType type = current_token_.type;

            if (type == TokenType::HASH) {
                auto result = parse_hash();
                if (result.is_error()) return Err<FileMetadata, std::string>(result.error());
                metadata.hash = result.value();

            } else if (type == TokenType::SIZE) {
                auto result = parse_size();
                if (result.is_error()) return Err<FileMetadata, std::string>(result.error());
                metadata.size = result.value();

            } else if (type == TokenType::MODIFIED) {
                auto result = parse_modified();
                if (result.is_error()) return Err<FileMetadata, std::string>(result.error());
                metadata.modified_time = result.value();

            } else if (type == TokenType::CREATED) {
                auto result = parse_created();
                if (result.is_error()) return Err<FileMetadata, std::string>(result.error());
                metadata.created_time = result.value();

            } else if (type == TokenType::STATE) {
                auto result = parse_state();
                if (result.is_error()) return Err<FileMetadata, std::string>(result.error());
                metadata.sync_state = result.value();

            } else if (type == TokenType::REPLICA) {
                auto result = parse_replica();
                if (result.is_error()) return Err<FileMetadata, std::string>(result.error());
                metadata.replicas.push_back(result.value());

            } else if (type == TokenType::FILE) {
                // Start of next file definition - stop here
                break;

            } else {
                // Unknown token - skip it or error
                return Err<FileMetadata, std::string>(
                    error_message("Unexpected token: " + current_token_.lexeme)
                );
            }
        }

        return Ok(metadata);
    }

    /**
     * Parse multiple file metadata definitions
     *
     * WHY THIS METHOD:
     * DDL files can contain multiple FILE definitions.
     * This method parses all of them.
     *
     * EXAMPLE:
     * FILE "/test1.txt" HASH "abc" SIZE 100
     * FILE "/test2.txt" HASH "def" SIZE 200
     * FILE "/test3.txt" HASH "ghi" SIZE 300
     *
     * @return Result<std::vector<FileMetadata>> - all parsed metadata
     */
    Result<std::vector<FileMetadata>> parse_all() {
        std::vector<FileMetadata> metadata_list;

        while (!is_at_end()) {
            auto result = parse_file_metadata();
            if (result.is_error()) {
                return Err<std::vector<FileMetadata>, std::string>(result.error());
            }
            metadata_list.push_back(result.value());
        }

        return Ok(metadata_list);
    }

private:
    Lexer lexer_;                  // Lexer for tokenizing input
    Token current_token_;          // Current token being examined
    Token previous_token_;         // Previous token (for error recovery)

    /**
     * Check if we've reached end of input
     * WHY: Need to know when to stop parsing
     */
    bool is_at_end() const {
        return current_token_.type == TokenType::END_OF_FILE;
    }

    /**
     * Move to next token
     *
     * WHY: After consuming a token, we need to advance to the next one
     */
    void advance() {
        previous_token_ = current_token_;
        current_token_ = lexer_.next_token();
    }

    /**
     * Check if current token matches expected type
     *
     * WHY: Need to verify grammar rules are followed
     *
     * @param type Expected token type
     * @return true if current token matches, false otherwise
     */
    bool check(TokenType type) const {
        if (is_at_end()) return false;
        return current_token_.type == type;
    }

    /**
     * Consume token if it matches expected type
     *
     * WHY: Common pattern in parsing - check and consume if match
     *
     * @param type Expected token type
     * @return true if consumed, false if didn't match
     */
    bool expect(TokenType type) {
        if (!check(type)) return false;
        advance();
        return true;
    }

    /**
     * Generate helpful error message with line/column info
     *
     * WHY: Good error messages help users fix DDL syntax errors
     *
     * EXAMPLE:
     * "Error at line 5, column 12: Expected SIZE keyword"
     */
    std::string error_message(const std::string& msg) const {
        std::ostringstream oss;
        oss << "Parse error at line " << current_token_.line
            << ", column " << current_token_.column
            << ": " << msg;
        return oss.str();
    }

    /**
     * Parse HASH attribute
     *
     * EXPECTED FORMAT: HASH "abc123..."
     *
     * WHY: Extract file hash from DDL
     */
    Result<std::string> parse_hash() {
        if (!expect(TokenType::HASH)) {
            return Err<std::string, std::string>(error_message("Expected HASH keyword"));
        }

        if (!expect(TokenType::STRING)) {
            return Err<std::string, std::string>(error_message("Expected hash string after HASH"));
        }

        return Ok(previous_token_.lexeme);
    }

    /**
     * Parse SIZE attribute
     *
     * EXPECTED FORMAT: SIZE 1024
     *
     * WHY: Extract file size from DDL
     */
    Result<uint64_t> parse_size() {
        if (!expect(TokenType::SIZE)) {
            return Err<uint64_t, std::string>(error_message("Expected SIZE keyword"));
        }

        if (!expect(TokenType::NUMBER)) {
            return Err<uint64_t, std::string>(error_message("Expected number after SIZE"));
        }

        try {
            uint64_t size = std::stoull(previous_token_.lexeme);
            return Ok(size);
        } catch (const std::exception& e) {
            return Err<uint64_t, std::string>(
                error_message("Invalid size number: " + previous_token_.lexeme)
            );
        }
    }

    /**
     * Parse MODIFIED attribute
     *
     * EXPECTED FORMAT: MODIFIED 1704096000
     *
     * WHY: Extract modification timestamp from DDL
     */
    Result<time_t> parse_modified() {
        if (!expect(TokenType::MODIFIED)) {
            return Err<time_t, std::string>(error_message("Expected MODIFIED keyword"));
        }

        if (!expect(TokenType::NUMBER)) {
            return Err<time_t, std::string>(error_message("Expected timestamp after MODIFIED"));
        }

        try {
            time_t timestamp = static_cast<time_t>(std::stoll(previous_token_.lexeme));
            return Ok(timestamp);
        } catch (const std::exception& e) {
            return Err<time_t, std::string>(
                error_message("Invalid timestamp: " + previous_token_.lexeme)
            );
        }
    }

    /**
     * Parse CREATED attribute
     *
     * EXPECTED FORMAT: CREATED 1704000000
     *
     * WHY: Extract creation timestamp from DDL
     */
    Result<time_t> parse_created() {
        if (!expect(TokenType::CREATED)) {
            return Err<time_t, std::string>(error_message("Expected CREATED keyword"));
        }

        if (!expect(TokenType::NUMBER)) {
            return Err<time_t, std::string>(error_message("Expected timestamp after CREATED"));
        }

        try {
            time_t timestamp = static_cast<time_t>(std::stoll(previous_token_.lexeme));
            return Ok(timestamp);
        } catch (const std::exception& e) {
            return Err<time_t, std::string>(
                error_message("Invalid timestamp: " + previous_token_.lexeme)
            );
        }
    }

    /**
     * Parse STATE attribute
     *
     * EXPECTED FORMAT: STATE SYNCED
     *                  STATE MODIFIED
     *                  STATE SYNCING
     *                  STATE CONFLICT
     *                  STATE DELETED
     *
     * WHY: Extract sync state from DDL
     */
    Result<SyncState> parse_state() {
        if (!expect(TokenType::STATE)) {
            return Err<SyncState, std::string>(error_message("Expected STATE keyword"));
        }

        TokenType type = current_token_.type;
        SyncState state;

        if (type == TokenType::SYNCED) {
            state = SyncState::SYNCED;
            advance();
        } else if (type == TokenType::SYNCING) {
            state = SyncState::SYNCING;
            advance();
        } else if (type == TokenType::CONFLICT) {
            state = SyncState::CONFLICT;
            advance();
        } else if (type == TokenType::DELETED) {
            state = SyncState::DELETED;
            advance();
        } else if (type == TokenType::STRING) {
            // Allow string representation of state
            std::string state_str = current_token_.lexeme;
            state = SyncStateUtils::from_string(state_str);
            advance();
        } else {
            return Err<SyncState, std::string>(
                error_message("Expected sync state after STATE keyword")
            );
        }

        return Ok(state);
    }

    /**
     * Parse REPLICA definition
     *
     * EXPECTED FORMAT: REPLICA "laptop_1" VERSION 5 MODIFIED 1704096000
     *
     * WHY: Extract replica information from DDL
     *
     * HOW IT WORKS:
     * 1. Expect REPLICA keyword
     * 2. Expect string (replica ID)
     * 3. Expect VERSION keyword + number
     * 4. Expect MODIFIED keyword + timestamp
     * 5. Return ReplicaInfo struct
     */
    Result<ReplicaInfo> parse_replica() {
        if (!expect(TokenType::REPLICA)) {
            return Err<ReplicaInfo, std::string>(error_message("Expected REPLICA keyword"));
        }

        ReplicaInfo replica;

        // Expect: <string> (replica ID)
        if (!expect(TokenType::STRING)) {
            return Err<ReplicaInfo, std::string>(
                error_message("Expected replica ID string after REPLICA")
            );
        }
        replica.replica_id = previous_token_.lexeme;

        // Expect: VERSION <number>
        if (!expect(TokenType::VERSION)) {
            return Err<ReplicaInfo, std::string>(
                error_message("Expected VERSION keyword in replica definition")
            );
        }

        if (!expect(TokenType::NUMBER)) {
            return Err<ReplicaInfo, std::string>(
                error_message("Expected version number after VERSION")
            );
        }

        try {
            replica.version = std::stoul(previous_token_.lexeme);
        } catch (const std::exception& e) {
            return Err<ReplicaInfo, std::string>(
                error_message("Invalid version number: " + previous_token_.lexeme)
            );
        }

        // Expect: MODIFIED <number>
        if (!expect(TokenType::MODIFIED)) {
            return Err<ReplicaInfo, std::string>(
                error_message("Expected MODIFIED keyword in replica definition")
            );
        }

        if (!expect(TokenType::NUMBER)) {
            return Err<ReplicaInfo, std::string>(
                error_message("Expected timestamp after MODIFIED")
            );
        }

        try {
            replica.modified_time = static_cast<time_t>(std::stoll(previous_token_.lexeme));
        } catch (const std::exception& e) {
            return Err<ReplicaInfo, std::string>(
                error_message("Invalid timestamp: " + previous_token_.lexeme)
            );
        }

        return Ok(replica);
    }
};

} // namespace metadata
} // namespace dfs
