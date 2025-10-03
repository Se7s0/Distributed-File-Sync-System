#pragma once

#include "http_types.hpp"
#include "dfs/core/result.hpp"
#include <string>
#include <cctype>

namespace dfs {
namespace network {

/**
 * @brief State machine states for HTTP request parsing
 *
 * HTTP parsing is done character-by-character using a state machine.
 * Each state represents where we are in the HTTP request format.
 *
 * Learning note: State machines are crucial for protocol parsing because:
 * 1. Network data arrives in chunks (we might not get the full request at once)
 * 2. We need to handle errors gracefully (malformed requests)
 * 3. It's memory efficient (we don't need to buffer the entire request)
 *
 * HTTP Request Format:
 * METHOD SP URL SP VERSION CRLF    <- Request line
 * Header-Name: Header-Value CRLF   <- Headers (multiple)
 * CRLF                             <- Empty line
 * [Body]                           <- Optional body
 */
enum class ParseState {
    METHOD,          // Parsing HTTP method (GET, POST, etc.)
    URL,             // Parsing request URL
    VERSION,         // Parsing HTTP version
    HEADER_NAME,     // Parsing header field name
    HEADER_VALUE,    // Parsing header field value
    BODY,            // Parsing request body
    COMPLETE,        // Parsing complete, request ready
    PARSE_ERROR      // Parsing error occurred (renamed to avoid Windows macro conflict)
};

/**
 * @brief HTTP request parser using state machine
 *
 * This parser can handle incremental data (streaming). You can feed it
 * data chunk by chunk as it arrives from the socket.
 *
 * Usage example:
 * ```cpp
 * HttpParser parser;
 * while (socket.has_data()) {
 *     auto data = socket.receive(1024);
 *     auto result = parser.parse(data.data(), data.size());
 *     if (result.is_error()) {
 *         // Handle parse error
 *     }
 *     if (result.value()) {
 *         // Parsing complete! Get the request
 *         HttpRequest request = parser.get_request();
 *     }
 * }
 * ```
 *
 * Learning note: This is a "non-blocking" parser - it doesn't wait for
 * all data to arrive. This is essential for high-performance servers.
 */
class HttpParser {
public:
    HttpParser() { reset(); }

    /**
     * @brief Parse incoming data
     *
     * Feed data to the parser byte by byte. The parser maintains internal
     * state and can handle data arriving in multiple chunks.
     *
     * @param data Pointer to incoming data buffer
     * @param len Length of data in bytes
     * @return Result containing true if parsing is complete, false if more data needed
     *         Returns error if parsing fails
     */
    Result<bool> parse(const char* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            char c = data[i];

            // Track position for error reporting
            if (c == '\n') {
                line_++;
                column_ = 0;
            } else {
                column_++;
            }

            // State machine - process one character
            switch (state_) {
                case ParseState::METHOD:
                    if (!parse_method(c)) {
                        return Err<bool, std::string>("Failed to parse HTTP method at line " +
                                       std::to_string(line_));
                    }
                    break;

                case ParseState::URL:
                    if (!parse_url(c)) {
                        return Err<bool, std::string>("Failed to parse URL at line " +
                                       std::to_string(line_));
                    }
                    break;

                case ParseState::VERSION:
                    if (!parse_version(c)) {
                        return Err<bool, std::string>("Failed to parse HTTP version at line " +
                                       std::to_string(line_));
                    }
                    break;

                case ParseState::HEADER_NAME:
                    if (!parse_header_name(c)) {
                        return Err<bool, std::string>("Failed to parse header name at line " +
                                       std::to_string(line_));
                    }
                    break;

                case ParseState::HEADER_VALUE:
                    if (!parse_header_value(c)) {
                        return Err<bool, std::string>("Failed to parse header value at line " +
                                       std::to_string(line_));
                    }
                    break;

                case ParseState::BODY:
                    parse_body(c);
                    break;

                case ParseState::COMPLETE:
                    return Ok(true);

                case ParseState::PARSE_ERROR:
                    return Err<bool, std::string>("Parser in error state");
            }

            // Check if parsing is complete
            if (state_ == ParseState::COMPLETE) {
                return Ok(true);
            }
        }

        // More data needed
        return Ok(false);
    }

    /**
     * @brief Get the parsed request
     *
     * Only call this after parse() returns true (parsing complete).
     *
     * @return The parsed HTTP request
     */
    HttpRequest get_request() const {
        return request_;
    }

    /**
     * @brief Check if parsing is complete
     */
    bool is_complete() const {
        return state_ == ParseState::COMPLETE;
    }

    /**
     * @brief Reset parser to initial state
     *
     * Call this to reuse the parser for a new request.
     */
    void reset() {
        state_ = ParseState::METHOD;
        request_ = HttpRequest();
        buffer_.clear();
        current_header_name_.clear();
        body_bytes_read_ = 0;
        line_ = 1;
        column_ = 0;
        last_char_was_cr_ = false;
    }

private:
    ParseState state_;
    HttpRequest request_;
    std::string buffer_;                // Temporary buffer for current token
    std::string current_header_name_;   // Current header name being parsed
    size_t body_bytes_read_;            // Number of body bytes read so far
    size_t line_;                       // Current line (for error reporting)
    size_t column_;                     // Current column (for error reporting)
    bool last_char_was_cr_;             // Track \r for CRLF detection

    /**
     * @brief Parse HTTP method (GET, POST, etc.)
     *
     * Method is terminated by a space character.
     * Example: "GET /index.html HTTP/1.1"
     *              ^-- we're here
     */
    bool parse_method(char c) {
        if (c == ' ') {
            // Space marks end of method
            if (buffer_.empty()) {
                return false; // Empty method
            }
            request_.method = HttpMethodUtils::from_string(buffer_);
            if (request_.method == HttpMethod::UNKNOWN) {
                return false; // Unknown HTTP method
            }
            buffer_.clear();
            state_ = ParseState::URL;
            return true;
        }

        // Method must be uppercase letters
        if (!std::isupper(static_cast<unsigned char>(c))) {
            return false;
        }

        buffer_ += c;
        return true;
    }

    /**
     * @brief Parse request URL
     *
     * URL is terminated by a space character.
     * Example: "GET /index.html HTTP/1.1"
     *                   ^-- we're here
     */
    bool parse_url(char c) {
        if (c == ' ') {
            // Space marks end of URL
            if (buffer_.empty()) {
                return false; // Empty URL
            }
            request_.url = buffer_;
            buffer_.clear();
            state_ = ParseState::VERSION;
            return true;
        }

        // URL can contain any printable character except space
        if (!std::isprint(static_cast<unsigned char>(c))) {
            return false;
        }

        buffer_ += c;
        return true;
    }

    /**
     * @brief Parse HTTP version
     *
     * Version is terminated by CRLF (\r\n).
     * Example: "GET /index.html HTTP/1.1\r\n"
     *                                ^-- we're here
     */
    bool parse_version(char c) {
        if (c == '\r') {
            last_char_was_cr_ = true;
            return true;
        }

        if (c == '\n' && last_char_was_cr_) {
            // CRLF marks end of request line
            if (buffer_ == "HTTP/1.1") {
                request_.version = HttpVersion::HTTP_1_1;
            } else if (buffer_ == "HTTP/1.0") {
                request_.version = HttpVersion::HTTP_1_0;
            } else {
                return false; // Unknown HTTP version
            }
            buffer_.clear();
            last_char_was_cr_ = false;
            state_ = ParseState::HEADER_NAME;
            return true;
        }

        last_char_was_cr_ = false;
        buffer_ += c;
        return true;
    }

    /**
     * @brief Parse header field name
     *
     * Header name is terminated by a colon (:).
     * Empty line (CRLF) indicates end of headers.
     *
     * Example: "Content-Type: text/html\r\n"
     *                     ^-- we're here
     */
    bool parse_header_name(char c) {
        // Check for empty line (end of headers)
        if (c == '\r') {
            last_char_was_cr_ = true;
            return true;
        }

        if (c == '\n' && last_char_was_cr_) {
            // Empty line - headers complete, check for body
            last_char_was_cr_ = false;

            // Check if request has a body (Content-Length header)
            std::string content_length = request_.get_header("Content-Length");
            if (!content_length.empty()) {
                size_t body_length = std::stoull(content_length);
                if (body_length > 0) {
                    request_.body.reserve(body_length);
                    state_ = ParseState::BODY;
                    return true;
                }
            }

            // No body, parsing complete
            state_ = ParseState::COMPLETE;
            return true;
        }

        last_char_was_cr_ = false;

        if (c == ':') {
            // Colon marks end of header name
            if (buffer_.empty()) {
                return false; // Empty header name
            }
            current_header_name_ = buffer_;
            buffer_.clear();
            state_ = ParseState::HEADER_VALUE;
            return true;
        }

        // Header names are typically alphanumeric with hyphens
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') {
            return false;
        }

        buffer_ += c;
        return true;
    }

    /**
     * @brief Parse header field value
     *
     * Header value is terminated by CRLF (\r\n).
     * Leading whitespace after colon is skipped.
     *
     * Example: "Content-Type: text/html\r\n"
     *                            ^-- we're here
     */
    bool parse_header_value(char c) {
        // Skip leading whitespace after colon
        if (buffer_.empty() && c == ' ') {
            return true;
        }

        if (c == '\r') {
            last_char_was_cr_ = true;
            return true;
        }

        if (c == '\n' && last_char_was_cr_) {
            // CRLF marks end of header
            request_.headers[current_header_name_] = buffer_;
            buffer_.clear();
            current_header_name_.clear();
            last_char_was_cr_ = false;
            state_ = ParseState::HEADER_NAME;
            return true;
        }

        last_char_was_cr_ = false;
        buffer_ += c;
        return true;
    }

    /**
     * @brief Parse request body
     *
     * Body length is determined by Content-Length header.
     * We read exactly that many bytes.
     */
    void parse_body(char c) {
        request_.body.push_back(static_cast<uint8_t>(c));
        body_bytes_read_++;

        // Check if we've read the entire body
        std::string content_length = request_.get_header("Content-Length");
        size_t expected_length = std::stoull(content_length);

        if (body_bytes_read_ >= expected_length) {
            state_ = ParseState::COMPLETE;
        }
    }
};

} // namespace network
} // namespace dfs