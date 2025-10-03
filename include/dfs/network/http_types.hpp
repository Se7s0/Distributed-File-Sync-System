#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <sstream>
#include <cstring>  // For _stricmp on Windows, strcasecmp on Unix

// For strcasecmp on Unix/Linux
#ifndef _WIN32
#include <strings.h>
#endif

namespace dfs {
namespace network {

/**
 * @brief HTTP request methods as defined in RFC 7231
 *
 * These are the most commonly used HTTP methods. Additional methods can be
 * added as needed (PUT, DELETE, PATCH, etc.)
 *
 * Learning note: Enums provide type-safe constants. enum class prevents
 * implicit conversions and name collisions.
 */
enum class HttpMethod {
    GET,     // Retrieve a resource
    POST,    // Submit data to be processed
    PUT,     // Update/create a resource
    DELETE_METHOD,  // Delete a resource (renamed to avoid Windows macro conflict)
    HEAD,    // Like GET but only returns headers
    OPTIONS, // Describe communication options
    UNKNOWN  // Fallback for unsupported methods
};

/**
 * @brief HTTP version enumeration
 *
 * Currently supports HTTP/1.0 and HTTP/1.1. HTTP/2 and HTTP/3 would require
 * different parsers and are beyond scope of this phase.
 */
enum class HttpVersion {
    HTTP_1_0,  // HTTP/1.0 - Simple, no keep-alive by default
    HTTP_1_1,  // HTTP/1.1 - Keep-alive by default, chunked encoding
    UNKNOWN
};

/**
 * @brief HTTP status codes as defined in RFC 7231
 *
 * These are the most common status codes. More specific codes can be added
 * as needed (e.g., 201, 204, 301, 304, 401, 403, etc.)
 */
enum class HttpStatus {
    OK = 200,                    // Request succeeded
    CREATED = 201,               // Resource created
    NO_CONTENT = 204,            // Success but no content to return
    BAD_REQUEST = 400,           // Client error - malformed request
    NOT_FOUND = 404,             // Resource not found
    METHOD_NOT_ALLOWED = 405,    // Method not supported for resource
    INTERNAL_SERVER_ERROR = 500, // Server error
    NOT_IMPLEMENTED = 501,       // Method not implemented
    SERVICE_UNAVAILABLE = 503    // Server overloaded or down
};

/**
 * @brief Represents an HTTP request
 *
 * Structure follows the HTTP/1.1 request format:
 * Request-Line = Method SP Request-URI SP HTTP-Version CRLF
 * Headers = *(header-field CRLF)
 * CRLF
 * [ message-body ]
 *
 * Example:
 * GET /index.html HTTP/1.1
 * Host: www.example.com
 * User-Agent: Mozilla/5.0
 *
 * [optional body]
 *
 * Learning note: Using std::vector<uint8_t> for body instead of std::string
 * because body might contain binary data (images, files, etc.)
 */
struct HttpRequest {
    HttpMethod method = HttpMethod::UNKNOWN;
    std::string url;                                      // Request URI (e.g., "/api/files")
    HttpVersion version = HttpVersion::HTTP_1_1;
    std::unordered_map<std::string, std::string> headers; // Case-sensitive headers
    std::vector<uint8_t> body;                           // Request body (optional)

    /**
     * @brief Get a header value (case-insensitive lookup)
     *
     * HTTP headers are case-insensitive per RFC 7230, but we store them
     * as-is. This helper performs case-insensitive lookup.
     *
     * @param name Header name (e.g., "Content-Type" or "content-type")
     * @return Header value if found, empty string otherwise
     */
    std::string get_header(const std::string& name) const {
        // Simple case-insensitive comparison
        for (const auto& [key, value] : headers) {
            if (strcasecmp_cross_platform(key.c_str(), name.c_str()) == 0) {
                return value;
            }
        }
        return "";
    }

    /**
     * @brief Check if a specific header exists
     */
    bool has_header(const std::string& name) const {
        return !get_header(name).empty();
    }

    /**
     * @brief Get the body as a string (for text content)
     *
     * Warning: Only use this if you know the body contains text!
     * For binary data, use the body vector directly.
     */
    std::string body_as_string() const {
        return std::string(body.begin(), body.end());
    }

private:
    // Cross-platform case-insensitive string comparison
    static int strcasecmp_cross_platform(const char* s1, const char* s2) {
#ifdef _WIN32
        return _stricmp(s1, s2);
#else
        return strcasecmp(s1, s2);
#endif
    }
};

/**
 * @brief Represents an HTTP response
 *
 * Structure follows the HTTP/1.1 response format:
 * Status-Line = HTTP-Version SP Status-Code SP Reason-Phrase CRLF
 * Headers = *(header-field CRLF)
 * CRLF
 * [ message-body ]
 *
 * Example:
 * HTTP/1.1 200 OK
 * Content-Type: text/html
 * Content-Length: 13
 *
 * Hello, World!
 */
struct HttpResponse {
    HttpVersion version = HttpVersion::HTTP_1_1;
    int status_code = 200;
    std::string reason_phrase;                            // e.g., "OK", "Not Found"
    std::unordered_map<std::string, std::string> headers;
    std::vector<uint8_t> body;

    /**
     * @brief Construct a response with a status code
     *
     * Automatically sets the reason phrase based on the status code.
     */
    HttpResponse() = default;

    explicit HttpResponse(HttpStatus status)
        : status_code(static_cast<int>(status))
        , reason_phrase(get_reason_phrase(status)) {
    }

    /**
     * @brief Set response body from a string
     *
     * This is a convenience method for text responses.
     * Automatically sets Content-Length header.
     *
     * @param content Text content to set as body
     */
    void set_body(const std::string& content) {
        body.assign(content.begin(), content.end());
        headers["Content-Length"] = std::to_string(body.size());
    }

    /**
     * @brief Set response body from binary data
     *
     * @param data Binary data to set as body
     */
    void set_body(const std::vector<uint8_t>& data) {
        body = data;
        headers["Content-Length"] = std::to_string(body.size());
    }

    /**
     * @brief Set a header value
     */
    void set_header(const std::string& name, const std::string& value) {
        headers[name] = value;
    }

    /**
     * @brief Serialize the response to bytes for transmission
     *
     * Converts the response object into the HTTP wire format.
     * This is what gets sent over the socket.
     *
     * Format:
     * HTTP/1.1 200 OK\r\n
     * Content-Type: text/html\r\n
     * Content-Length: 13\r\n
     * \r\n
     * Hello, World!
     *
     * @return Serialized response as byte vector
     */
    std::vector<uint8_t> serialize() const {
        std::ostringstream oss;

        // Status line
        oss << version_to_string(version) << " "
            << status_code << " "
            << reason_phrase << "\r\n";

        // Headers
        for (const auto& [name, value] : headers) {
            oss << name << ": " << value << "\r\n";
        }

        // Empty line separates headers from body
        oss << "\r\n";

        // Convert to bytes
        std::string header_str = oss.str();
        std::vector<uint8_t> result(header_str.begin(), header_str.end());

        // Append body
        result.insert(result.end(), body.begin(), body.end());

        return result;
    }

    /**
     * @brief Get the standard reason phrase for a status code
     */
    static std::string get_reason_phrase(HttpStatus status) {
        switch (status) {
            case HttpStatus::OK: return "OK";
            case HttpStatus::CREATED: return "Created";
            case HttpStatus::NO_CONTENT: return "No Content";
            case HttpStatus::BAD_REQUEST: return "Bad Request";
            case HttpStatus::NOT_FOUND: return "Not Found";
            case HttpStatus::METHOD_NOT_ALLOWED: return "Method Not Allowed";
            case HttpStatus::INTERNAL_SERVER_ERROR: return "Internal Server Error";
            case HttpStatus::NOT_IMPLEMENTED: return "Not Implemented";
            case HttpStatus::SERVICE_UNAVAILABLE: return "Service Unavailable";
            default: return "Unknown";
        }
    }

    /**
     * @brief Convert HTTP version to string representation
     */
    static std::string version_to_string(HttpVersion version) {
        switch (version) {
            case HttpVersion::HTTP_1_0: return "HTTP/1.0";
            case HttpVersion::HTTP_1_1: return "HTTP/1.1";
            default: return "HTTP/1.1";
        }
    }
};

/**
 * @brief Helper functions for HTTP method conversions
 */
class HttpMethodUtils {
public:
    /**
     * @brief Convert string to HttpMethod enum
     *
     * @param method_str Method as string (e.g., "GET", "POST")
     * @return Corresponding HttpMethod enum value
     */
    static HttpMethod from_string(const std::string& method_str) {
        if (method_str == "GET") return HttpMethod::GET;
        if (method_str == "POST") return HttpMethod::POST;
        if (method_str == "PUT") return HttpMethod::PUT;
        if (method_str == "DELETE") return HttpMethod::DELETE_METHOD;
        if (method_str == "HEAD") return HttpMethod::HEAD;
        if (method_str == "OPTIONS") return HttpMethod::OPTIONS;
        return HttpMethod::UNKNOWN;
    }

    /**
     * @brief Convert HttpMethod enum to string
     *
     * @param method HttpMethod enum value
     * @return Method as string
     */
    static std::string to_string(HttpMethod method) {
        switch (method) {
            case HttpMethod::GET: return "GET";
            case HttpMethod::POST: return "POST";
            case HttpMethod::PUT: return "PUT";
            case HttpMethod::DELETE_METHOD: return "DELETE";
            case HttpMethod::HEAD: return "HEAD";
            case HttpMethod::OPTIONS: return "OPTIONS";
            default: return "UNKNOWN";
        }
    }
};

} // namespace network
} // namespace dfs
