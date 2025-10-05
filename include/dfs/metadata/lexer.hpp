#pragma once

/**
 * @file lexer.hpp
 * @brief Lexer (tokenizer) for metadata DDL parsing
 *
 * WHY THIS FILE EXISTS:
 * Parsing is a two-step process: Lexing → Parsing
 * 1. Lexer (this file): Converts text into tokens
 * 2. Parser (parser.hpp): Converts tokens into data structures
 *
 * EXAMPLE:
 * Input text:  'FILE "/docs/test.txt" HASH "abc123"'
 * Tokens:      [FILE] [STRING:/docs/test.txt] [HASH] [STRING:abc123]
 * Parse result: FileMetadata { file_path="/docs/test.txt", hash="abc123" }
 *
 * WHY SEPARATE LEXER AND PARSER:
 * - Separation of concerns: Lexer handles character-level details, parser handles grammar
 * - Easier testing: Can test lexer and parser independently
 * - Standard compiler design: This is how GCC, Clang, etc. work
 *
 * HOW IT INTEGRATES:
 * Parser creates Lexer → Lexer tokenizes input → Parser consumes tokens
 *
 * DESIGN DECISIONS:
 * - Simple state machine: No regex, just character-by-character scanning
 * - Peek functionality: Parser needs to look ahead without consuming tokens
 * - Line/column tracking: For error messages ("Error at line 5, column 12")
 */

#include <string>
#include <vector>
#include <cctype>

namespace dfs {
namespace metadata {

/**
 * @brief Token types in our DDL language
 *
 * WHY THIS ENUM:
 * The lexer needs to classify each piece of text into a token type.
 * This enum defines all possible token types in our DDL.
 *
 * TOKEN CATEGORIES:
 * - Keywords: FILE, HASH, SIZE, etc. (reserved words with special meaning)
 * - Literals: STRING, NUMBER (actual data values)
 * - Control: EOF (end of file marker)
 *
 * EXAMPLE DDL:
 * FILE "/test.txt" HASH "abc" SIZE 100
 * └──┘ └─────────┘ └──┘ └─┘ └──┘ └─┘
 *  │       │        │    │    │    └─ NUMBER
 *  │       │        │    │    └────── SIZE keyword
 *  │       │        │    └─────────── STRING
 *  │       │        └──────────────── HASH keyword
 *  │       └───────────────────────── STRING
 *  └───────────────────────────────── FILE keyword
 */
enum class TokenType {
    // Keywords (reserved words in DDL)
    FILE,           // FILE keyword (starts file definition)
    HASH,           // HASH keyword
    SIZE,           // SIZE keyword
    MODIFIED,       // MODIFIED keyword
    CREATED,        // CREATED keyword
    STATE,          // STATE keyword
    REPLICA,        // REPLICA keyword
    VERSION,        // VERSION keyword

    // Sync state keywords
    SYNCED,         // SYNCED state
    MODIFIED_STATE, // MODIFIED state (renamed to avoid conflict with MODIFIED keyword)
    SYNCING,        // SYNCING state
    CONFLICT,       // CONFLICT state
    DELETED,        // DELETED state

    // Literals (actual values)
    STRING,         // String literal (e.g., "/test.txt", "abc123")
    NUMBER,         // Number literal (e.g., 100, 1704096000)

    // Control
    END_OF_FILE,    // End of input
    UNKNOWN         // Unrecognized token (for error handling)
};

/**
 * @brief Represents a single token
 *
 * WHY THIS STRUCT:
 * A token is more than just a type - it also has:
 * - The actual text (lexeme): "100" or "/test.txt"
 * - Position in source: For error messages
 *
 * EXAMPLE:
 * Input: 'SIZE 100'
 * Tokens:
 *   Token { type=SIZE, lexeme="SIZE", line=1, column=1 }
 *   Token { type=NUMBER, lexeme="100", line=1, column=6 }
 */
struct Token {
    TokenType type;        // What kind of token is this?
    std::string lexeme;    // The actual text (e.g., "100", "/test.txt")
    size_t line;           // Line number (for error messages)
    size_t column;         // Column number (for error messages)

    /**
     * Constructor
     * WHY: Convenient way to create tokens
     */
    Token(TokenType t = TokenType::UNKNOWN,
          const std::string& lex = "",
          size_t ln = 0,
          size_t col = 0)
        : type(t), lexeme(lex), line(ln), column(col) {}

    /**
     * Check if this token is a keyword
     * WHY: Parser needs to distinguish keywords from literals
     */
    bool is_keyword() const {
        return type != TokenType::STRING &&
               type != TokenType::NUMBER &&
               type != TokenType::END_OF_FILE &&
               type != TokenType::UNKNOWN;
    }
};

/**
 * @brief Lexer (tokenizer) for metadata DDL
 *
 * WHY THIS CLASS:
 * Converts raw text into a stream of tokens for the parser to consume.
 *
 * HOW IT WORKS (State Machine):
 * 1. Start at position 0
 * 2. Skip whitespace
 * 3. Look at current character:
 *    - Letter? → Scan identifier/keyword
 *    - Digit? → Scan number
 *    - Quote? → Scan string
 *    - Other? → Error
 * 4. Return token
 * 5. Advance position and repeat
 *
 * EXAMPLE USAGE:
 * Lexer lexer("FILE \"/test.txt\" SIZE 100");
 * Token t1 = lexer.next_token();  // FILE
 * Token t2 = lexer.next_token();  // STRING "/test.txt"
 * Token t3 = lexer.next_token();  // SIZE
 * Token t4 = lexer.next_token();  // NUMBER 100
 */
class Lexer {
public:
    /**
     * Constructor
     *
     * @param input The DDL text to tokenize
     *
     * WHY: Initialize the lexer with input text and set position to 0
     */
    explicit Lexer(const std::string& input)
        : input_(input), position_(0), line_(1), column_(1) {}

    /**
     * Get the next token from the input
     *
     * WHY THIS METHOD:
     * This is the main interface for the lexer. Parser calls this repeatedly
     * to get tokens one by one.
     *
     * HOW IT WORKS:
     * 1. Skip whitespace/comments
     * 2. If at end → return EOF token
     * 3. Look at current char and decide what to scan
     * 4. Scan complete token and return it
     *
     * @return Next token in the input stream
     */
    Token next_token() {
        skip_whitespace();

        if (is_at_end()) {
            return Token(TokenType::END_OF_FILE, "", line_, column_);
        }

        char c = peek();

        // String literal (starts with quote)
        if (c == '"') {
            return scan_string();
        }

        // Number (starts with digit)
        if (std::isdigit(c)) {
            return scan_number();
        }

        // Keyword or identifier (starts with letter)
        if (std::isalpha(c) || c == '_') {
            return scan_keyword();
        }

        // Unknown character
        advance();
        return Token(TokenType::UNKNOWN, std::string(1, c), line_, column_ - 1);
    }

    /**
     * Peek at the next token without consuming it
     *
     * WHY THIS METHOD:
     * Parser sometimes needs to look ahead to decide what to do.
     * Example: "Is the next token SIZE or MODIFIED?"
     *
     * HOW IT WORKS:
     * Save current position → get next token → restore position
     *
     * @return Next token (without advancing position)
     */
    Token peek_token() {
        // Save current state
        size_t saved_pos = position_;
        size_t saved_line = line_;
        size_t saved_col = column_;

        // Get next token
        Token token = next_token();

        // Restore state
        position_ = saved_pos;
        line_ = saved_line;
        column_ = saved_col;

        return token;
    }

    /**
     * Get current line number
     * WHY: For error messages
     */
    size_t current_line() const { return line_; }

    /**
     * Get current column number
     * WHY: For error messages
     */
    size_t current_column() const { return column_; }

private:
    std::string input_;    // Input text to tokenize
    size_t position_;      // Current position in input
    size_t line_;          // Current line number
    size_t column_;        // Current column number

    /**
     * Check if we've reached the end of input
     * WHY: Need to know when to stop tokenizing
     */
    bool is_at_end() const {
        return position_ >= input_.length();
    }

    /**
     * Look at current character without consuming it
     * WHY: Need to examine character before deciding what to do
     */
    char peek() const {
        if (is_at_end()) return '\0';
        return input_[position_];
    }

    /**
     * Look ahead N characters without consuming
     * WHY: Sometimes need to check multiple characters ahead
     */
    char peek_ahead(size_t n) const {
        if (position_ + n >= input_.length()) return '\0';
        return input_[position_ + n];
    }

    /**
     * Consume current character and move to next
     *
     * WHY: After examining a character, we need to move past it
     * Also updates line/column for error messages
     *
     * @return The character that was consumed
     */
    char advance() {
        if (is_at_end()) return '\0';

        char c = input_[position_++];

        // Track line/column for error messages
        if (c == '\n') {
            line_++;
            column_ = 1;
        } else {
            column_++;
        }

        return c;
    }

    /**
     * Skip whitespace and comments
     *
     * WHY: Whitespace is not significant in our DDL
     * We allow # comments for readability in DDL files
     *
     * EXAMPLE:
     * FILE "/test.txt"  # This is a comment
     * ^^^^^ whitespace between FILE and string is skipped
     */
    void skip_whitespace() {
        while (!is_at_end()) {
            char c = peek();

            if (std::isspace(c)) {
                advance();
            } else if (c == '#') {
                // Skip comment until end of line
                while (!is_at_end() && peek() != '\n') {
                    advance();
                }
            } else {
                break;
            }
        }
    }

    /**
     * Scan a string literal
     *
     * WHY: Need to extract quoted strings like "/test.txt" or "abc123"
     *
     * HOW IT WORKS:
     * 1. Consume opening quote
     * 2. Read characters until closing quote
     * 3. Handle escape sequences (\n, \t, \", \\)
     * 4. Return STRING token
     *
     * EXAMPLE:
     * Input: "hello\nworld"
     * Output: Token { type=STRING, lexeme="hello\nworld" }
     */
    Token scan_string() {
        size_t start_line = line_;
        size_t start_col = column_;

        advance();  // Consume opening quote

        std::string value;
        while (!is_at_end() && peek() != '"') {
            char c = peek();

            // Handle escape sequences
            if (c == '\\') {
                advance();
                if (is_at_end()) break;

                char escaped = peek();
                switch (escaped) {
                    case 'n': value += '\n'; break;
                    case 't': value += '\t'; break;
                    case 'r': value += '\r'; break;
                    case '"': value += '"'; break;
                    case '\\': value += '\\'; break;
                    default: value += escaped; break;
                }
                advance();
            } else {
                value += c;
                advance();
            }
        }

        // Consume closing quote
        if (!is_at_end() && peek() == '"') {
            advance();
        }

        return Token(TokenType::STRING, value, start_line, start_col);
    }

    /**
     * Scan a number literal
     *
     * WHY: Need to extract numbers like 100, 1704096000
     *
     * HOW IT WORKS:
     * Read consecutive digits and return NUMBER token
     *
     * EXAMPLE:
     * Input: 1704096000
     * Output: Token { type=NUMBER, lexeme="1704096000" }
     */
    Token scan_number() {
        size_t start_line = line_;
        size_t start_col = column_;

        std::string value;
        while (!is_at_end() && std::isdigit(peek())) {
            value += advance();
        }

        return Token(TokenType::NUMBER, value, start_line, start_col);
    }

    /**
     * Scan a keyword or identifier
     *
     * WHY: Need to recognize keywords like FILE, HASH, SIZE, etc.
     *
     * HOW IT WORKS:
     * 1. Read consecutive letters/digits/underscores
     * 2. Check if it's a keyword (FILE, HASH, etc.)
     * 3. If yes → return keyword token
     * 4. If no → return UNKNOWN (we don't allow arbitrary identifiers)
     *
     * EXAMPLE:
     * Input: FILE → Token { type=FILE, lexeme="FILE" }
     * Input: HASH → Token { type=HASH, lexeme="HASH" }
     * Input: foobar → Token { type=UNKNOWN, lexeme="foobar" }
     */
    Token scan_keyword() {
        size_t start_line = line_;
        size_t start_col = column_;

        std::string value;
        while (!is_at_end() && (std::isalnum(peek()) || peek() == '_')) {
            value += advance();
        }

        // Convert to token type
        TokenType type = keyword_type(value);

        return Token(type, value, start_line, start_col);
    }

    /**
     * Map keyword string to token type
     *
     * WHY: Need to convert strings like "FILE" to TokenType::FILE
     *
     * HOW IT WORKS:
     * Simple if-else chain checking all keywords
     */
    TokenType keyword_type(const std::string& keyword) const {
        // File metadata keywords
        if (keyword == "FILE") return TokenType::FILE;
        if (keyword == "HASH") return TokenType::HASH;
        if (keyword == "SIZE") return TokenType::SIZE;
        if (keyword == "MODIFIED") return TokenType::MODIFIED;
        if (keyword == "CREATED") return TokenType::CREATED;
        if (keyword == "STATE") return TokenType::STATE;
        if (keyword == "REPLICA") return TokenType::REPLICA;
        if (keyword == "VERSION") return TokenType::VERSION;

        // Sync state keywords
        if (keyword == "SYNCED") return TokenType::SYNCED;
        if (keyword == "SYNCING") return TokenType::SYNCING;
        if (keyword == "CONFLICT") return TokenType::CONFLICT;
        if (keyword == "DELETED") return TokenType::DELETED;

        // Unknown keyword
        return TokenType::UNKNOWN;
    }
};

/**
 * @brief Helper functions for token type conversions
 *
 * WHY: For logging and debugging
 */
class TokenTypeUtils {
public:
    /**
     * Convert token type to string
     * WHY: For error messages and debugging
     */
    static std::string to_string(TokenType type) {
        switch (type) {
            case TokenType::FILE: return "FILE";
            case TokenType::HASH: return "HASH";
            case TokenType::SIZE: return "SIZE";
            case TokenType::MODIFIED: return "MODIFIED";
            case TokenType::CREATED: return "CREATED";
            case TokenType::STATE: return "STATE";
            case TokenType::REPLICA: return "REPLICA";
            case TokenType::VERSION: return "VERSION";
            case TokenType::SYNCED: return "SYNCED";
            case TokenType::MODIFIED_STATE: return "MODIFIED_STATE";
            case TokenType::SYNCING: return "SYNCING";
            case TokenType::CONFLICT: return "CONFLICT";
            case TokenType::DELETED: return "DELETED";
            case TokenType::STRING: return "STRING";
            case TokenType::NUMBER: return "NUMBER";
            case TokenType::END_OF_FILE: return "EOF";
            case TokenType::UNKNOWN: return "UNKNOWN";
            default: return "UNKNOWN";
        }
    }
};

} // namespace metadata
} // namespace dfs
