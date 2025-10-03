# Phase 1 Reference - HTTP Server & Client Implementation

## Overview

**Phase:** 1 - HTTP Server & Client
**Duration:** 3-5 days
**Difficulty:** Medium
**Status:** ✅ Complete

This phase implements a working HTTP/1.1 server from scratch in C++, including:
- HTTP protocol parsing (state machine)
- Request/Response handling
- Multi-connection support
- Complete example server with routing

---

## Table of Contents

1. [What We Built](#what-we-built)
2. [Architecture](#architecture)
3. [File Structure](#file-structure)
4. [Core Components](#core-components)
5. [How to Build](#how-to-build)
6. [How to Test](#how-to-test)
7. [Code Walkthrough](#code-walkthrough)
8. [Learning Points](#learning-points)
9. [How This Contributes](#how-this-contributes)
10. [Why This Is Crucial](#why-this-is-crucial)
11. [Next Steps](#next-steps)

---

## What We Built

In Phase 1, we implemented:

### 1. HTTP Types System (`http_types.hpp`)
- **HttpMethod** enum: GET, POST, PUT, DELETE, HEAD, OPTIONS
- **HttpVersion** enum: HTTP/1.0, HTTP/1.1
- **HttpStatus** enum: Common status codes (200, 404, 500, etc.)
- **HttpRequest** struct: Represents incoming HTTP requests
- **HttpResponse** struct: Represents outgoing HTTP responses
- **Helper utilities**: Method conversion, header handling, body serialization

### 2. HTTP Parser (`http_parser.hpp`)
- **State machine-based parser**: Handles incremental data
- **ParseState** enum: METHOD, URL, VERSION, HEADER_NAME, HEADER_VALUE, BODY, COMPLETE, ERROR
- **Streaming support**: Can process data chunk-by-chunk
- **Error handling**: Graceful error reporting with line/column numbers
- **Standards-compliant**: Follows HTTP/1.1 RFC 7230-7235

### 3. HTTP Server (`http_server.hpp/cpp`)
- **Connection handling**: Accept and process client connections
- **Request routing**: User-defined request handlers
- **Response generation**: Serialize and send responses
- **Error responses**: Automatic error page generation
- **Graceful shutdown**: SIGINT handling

### 4. Example Server (`http_server_example.cpp`)
- **Multiple routes**: /, /hello, /info, /echo, /headers
- **Different response types**: HTML, plain text, JSON
- **Request inspection**: Echo and header display endpoints
- **Production-ready**: Logging, error handling, command-line arguments

---

## Architecture

### High-Level Flow

```
┌─────────────┐
│   Client    │
│  (Browser/  │
│    curl)    │
└──────┬──────┘
       │ HTTP Request
       │ GET /hello HTTP/1.1
       │ Host: localhost:8080
       │ ...
       ▼
┌─────────────────────────────────────┐
│         HttpServer                  │
│                                     │
│  ┌──────────────────────────────┐  │
│  │ 1. Accept Connection         │  │
│  │    (Socket::accept)          │  │
│  └────────────┬─────────────────┘  │
│               ▼                     │
│  ┌──────────────────────────────┐  │
│  │ 2. Read & Parse Request      │  │
│  │    (HttpParser)              │  │
│  │    - State machine parsing   │  │
│  │    - Incremental processing  │  │
│  └────────────┬─────────────────┘  │
│               ▼                     │
│  ┌──────────────────────────────┐  │
│  │ 3. Call User Handler         │  │
│  │    (HttpRequestHandler)      │  │
│  │    - Route matching          │  │
│  │    - Business logic          │  │
│  └────────────┬─────────────────┘  │
│               ▼                     │
│  ┌──────────────────────────────┐  │
│  │ 4. Send Response             │  │
│  │    (HttpResponse::serialize) │  │
│  └────────────┬─────────────────┘  │
│               ▼                     │
│  ┌──────────────────────────────┐  │
│  │ 5. Close Connection          │  │
│  └──────────────────────────────┘  │
└─────────────────────────────────────┘
       │ HTTP Response
       │ HTTP/1.1 200 OK
       │ Content-Type: text/plain
       │ ...
       │ Hello from DFS!
       ▼
┌─────────────┐
│   Client    │
└─────────────┘
```

### State Machine Diagram (HTTP Parser)

```
START
  │
  ▼
┌────────┐
│ METHOD │  Read until space → "GET", "POST", etc.
└───┬────┘
    │
    ▼
┌────────┐
│  URL   │  Read until space → "/index.html", "/api/users"
└───┬────┘
    │
    ▼
┌──────────┐
│ VERSION  │  Read until CRLF → "HTTP/1.1"
└────┬─────┘
     │
     ▼
┌──────────────┐
│ HEADER_NAME  │◄──┐ Read until ':' → "Content-Type"
└───┬──────────┘   │
    │              │
    ▼              │
┌──────────────┐   │
│ HEADER_VALUE │───┘ Read until CRLF → "application/json"
└───┬──────────┘
    │
    │ Empty line (CRLF)?
    ├─ Yes (Content-Length > 0) ──▼
    │                          ┌──────┐
    │                          │ BODY │ Read Content-Length bytes
    │                          └──┬───┘
    │                             │
    └─ Yes (No body) ─────────────┼──────▼
                                  │  ┌──────────┐
                                  └─▶│ COMPLETE │
                                     └──────────┘
```

---

## File Structure

```
Distributed-File-Sync-System/
├── include/dfs/network/
│   ├── http_types.hpp       # HTTP data structures
│   ├── http_parser.hpp      # Request parser (state machine)
│   └── http_server.hpp      # Server class
│
├── src/network/
│   ├── http_server.cpp      # Server implementation
│   └── CMakeLists.txt       # Updated to include http_server.cpp
│
├── examples/
│   ├── http_server_example.cpp  # Complete working example
│   └── CMakeLists.txt           # Updated to build example
│
└── phase_1_reference.md     # This file
```

---

## Core Components

### 1. HTTP Types (`http_types.hpp`)

#### HttpRequest Structure
```cpp
struct HttpRequest {
    HttpMethod method;                                    // GET, POST, etc.
    std::string url;                                      // "/api/users"
    HttpVersion version;                                  // HTTP/1.1
    std::unordered_map<std::string, std::string> headers; // {"Host": "example.com"}
    std::vector<uint8_t> body;                           // Request body (binary-safe)

    std::string get_header(const std::string& name) const;
    bool has_header(const std::string& name) const;
    std::string body_as_string() const;
};
```

**Why these design choices?**
- **`std::vector<uint8_t>` for body**: Binary-safe, can handle images/files, not just text
- **Case-insensitive headers**: HTTP headers are case-insensitive per RFC 7230
- **Separate method enum**: Type-safe, prevents typos like "GTE" instead of "GET"

#### HttpResponse Structure
```cpp
struct HttpResponse {
    HttpVersion version;
    int status_code;
    std::string reason_phrase;
    std::unordered_map<std::string, std::string> headers;
    std::vector<uint8_t> body;

    explicit HttpResponse(HttpStatus status);
    void set_body(const std::string& content);
    void set_header(const std::string& name, const std::string& value);
    std::vector<uint8_t> serialize() const;  // Convert to wire format
};
```

**Key method: `serialize()`**
```cpp
std::vector<uint8_t> HttpResponse::serialize() const {
    // Produces:
    // HTTP/1.1 200 OK\r\n
    // Content-Type: text/html\r\n
    // Content-Length: 13\r\n
    // \r\n
    // Hello, World!
}
```

### 2. HTTP Parser (`http_parser.hpp`)

#### The Parser Class
```cpp
class HttpParser {
public:
    HttpParser();

    // Feed data to parser, returns true when complete
    Result<bool> parse(const char* data, size_t len);

    // Get parsed request (only call when parse returns true)
    HttpRequest get_request() const;

    // Check parsing state
    bool is_complete() const;

    // Reset for reuse
    void reset();

private:
    ParseState state_;
    HttpRequest request_;
    std::string buffer_;
    // ... helper state
};
```

**Why incremental parsing?**
Network data arrives in chunks. You might receive:
1. First chunk: `"GET /hello HT"`
2. Second chunk: `"TP/1.1\r\nHost:"`
3. Third chunk: `" localhost\r\n\r\n"`

The parser handles this gracefully without buffering everything.

#### Example Usage
```cpp
HttpParser parser;
Socket client = /* accepted connection */;

while (!parser.is_complete()) {
    auto data = client.receive(4096);  // Read up to 4KB
    auto result = parser.parse(data.data(), data.size());

    if (result.is_error()) {
        // Parse error
        spdlog::error("Parse failed: {}", result.error());
        break;
    }

    if (result.value()) {
        // Parsing complete!
        HttpRequest request = parser.get_request();
        // Process request...
        break;
    }
    // Need more data, loop continues
}
```

### 3. HTTP Server (`http_server.hpp/cpp`)

#### Server Class
```cpp
class HttpServer {
public:
    void set_handler(HttpRequestHandler handler);
    Result<void> listen(uint16_t port, const std::string& address = "0.0.0.0");
    Result<void> serve_forever();
    void stop();

private:
    Socket listener_;
    HttpRequestHandler handler_;
    bool running_;

    Result<void> handle_connection(std::unique_ptr<Socket> client);
    Result<HttpRequest> read_request(Socket& socket, HttpParser& parser);
    Result<void> send_response(Socket& socket, const HttpResponse& response);
    HttpResponse create_error_response(HttpStatus status, const std::string& message);
};
```

#### Server Main Loop (`serve_forever`)
```cpp
Result<void> HttpServer::serve_forever() {
    running_ = true;

    while (running_) {
        // 1. Accept connection (blocks)
        auto client = listener_.accept();

        // 2. Handle connection
        handle_connection(std::move(client));

        // 3. Connection closed, loop continues
    }

    return Ok();
}
```

#### Connection Handler
```cpp
Result<void> HttpServer::handle_connection(std::unique_ptr<Socket> client) {
    // 1. Read and parse request
    HttpParser parser;
    auto request = read_request(*client, parser);
    if (request.is_error()) {
        send_response(*client, create_error_response(
            HttpStatus::BAD_REQUEST,
            request.error()
        ));
        return Err<void>(request.error());
    }

    // 2. Call user handler
    HttpResponse response;
    try {
        response = handler_(request.value());
    } catch (const std::exception& e) {
        response = create_error_response(
            HttpStatus::INTERNAL_SERVER_ERROR,
            "Handler error"
        );
    }

    // 3. Send response
    send_response(*client, response);

    // 4. Close connection
    client->close();

    return Ok();
}
```

**Phase 1 Limitation: Synchronous Processing**
- One connection at a time
- Server blocks while handling each request
- Good enough for learning and testing
- Later phases will add async I/O and thread pools

---

## How to Build

### Prerequisites
```bash
# Linux/WSL
sudo apt install build-essential cmake git

# macOS
brew install cmake
```

### Build Steps
```bash
# 1. Navigate to project directory
cd /path/to/Distributed-File-Sync-System

# 2. Create build directory
mkdir -p build && cd build

# 3. Configure (downloads dependencies: spdlog, nlohmann/json, googletest)
cmake .. -DCMAKE_BUILD_TYPE=Debug

# 4. Build (parallel build with 4 jobs)
cmake --build . -j4

# 5. Verify build
ls examples/
# Should see: socket_example  http_server_example
```

### Build Configuration Options
```bash
# Release build (optimized)
cmake .. -DCMAKE_BUILD_TYPE=Release

# Disable tests
cmake .. -DBUILD_TESTS=OFF

# Disable examples
cmake .. -DBUILD_EXAMPLES=OFF
```

---

## How to Test

### Starting the Server

#### Method 1: Run directly
```bash
# From project root
./build/examples/http_server_example

# Or specify port
./build/examples/http_server_example 9090
```

Expected output:
```
=================================
DFS HTTP Server Example - Phase 1
=================================

[HH:MM:SS] [info] HTTP server listening on 0.0.0.0:8080
[HH:MM:SS] [info] Server started successfully!
[HH:MM:SS] [info] Access the server at: http://localhost:8080
[HH:MM:SS] [info]
[HH:MM:SS] [info] Test with curl:
[HH:MM:SS] [info]   curl http://localhost:8080/hello
[HH:MM:SS] [info]   curl http://localhost:8080/info
[HH:MM:SS] [info]   curl -X POST http://localhost:8080/echo -d 'Hello!'
[HH:MM:SS] [info]
[HH:MM:SS] [info] Press Ctrl+C to stop
[HH:MM:SS] [info]
[HH:MM:SS] [info] Server started. Waiting for connections...
```

### Testing Endpoints

#### 1. Browser Test
Open in browser: `http://localhost:8080/`

You should see a welcome page with all available endpoints.

#### 2. Command Line Tests

**Test 1: Simple GET**
```bash
curl http://localhost:8080/hello
```
Expected:
```
Hello from DFS HTTP Server!
```

**Test 2: JSON Response**
```bash
curl http://localhost:8080/info
```
Expected:
```json
{
  "server": "DFS HTTP Server",
  "version": "1.0.0",
  "phase": "Phase 1 - HTTP Implementation",
  "features": [
    "HTTP/1.1 parsing",
    "GET/POST support",
    "Custom routing",
    "Header handling"
  ]
}
```

**Test 3: POST Request (Echo)**
```bash
curl -X POST http://localhost:8080/echo -d "Hello from client!"
```
Expected:
```
You sent: Hello from client!
```

**Test 4: Header Inspection**
```bash
curl http://localhost:8080/headers
```
Expected:
```
Request Headers:
=================

Host: localhost:8080
User-Agent: curl/7.81.0
Accept: */*
```

**Test 5: 404 Not Found**
```bash
curl http://localhost:8080/nonexistent
```
Expected: HTML error page

**Test 6: Verbose Mode (See HTTP Details)**
```bash
curl -v http://localhost:8080/hello
```
Expected:
```
* Trying 127.0.0.1:8080...
* Connected to localhost (127.0.0.1) port 8080 (#0)
> GET /hello HTTP/1.1
> Host: localhost:8080
> User-Agent: curl/7.81.0
> Accept: */*
>
< HTTP/1.1 200 OK
< Content-Type: text/plain
< Content-Length: 31
<
Hello from DFS HTTP Server!
```

#### 3. Automated Testing Script

Create `test_phase1.sh`:
```bash
#!/bin/bash

SERVER="http://localhost:8080"

echo "Testing Phase 1 HTTP Server"
echo "==========================="

# Test 1: GET /hello
echo -n "Test 1: GET /hello ... "
RESULT=$(curl -s $SERVER/hello)
if [[ "$RESULT" == *"Hello from DFS"* ]]; then
    echo "✓ PASS"
else
    echo "✗ FAIL"
fi

# Test 2: GET /info (JSON)
echo -n "Test 2: GET /info ... "
RESULT=$(curl -s $SERVER/info)
if [[ "$RESULT" == *"\"server\": \"DFS HTTP Server\""* ]]; then
    echo "✓ PASS"
else
    echo "✗ FAIL"
fi

# Test 3: POST /echo
echo -n "Test 3: POST /echo ... "
RESULT=$(curl -s -X POST $SERVER/echo -d "test")
if [[ "$RESULT" == *"You sent: test"* ]]; then
    echo "✓ PASS"
else
    echo "✗ FAIL"
fi

# Test 4: 404 Not Found
echo -n "Test 4: 404 handling ... "
STATUS=$(curl -s -o /dev/null -w "%{http_code}" $SERVER/nonexistent)
if [[ "$STATUS" == "404" ]]; then
    echo "✓ PASS"
else
    echo "✗ FAIL (got $STATUS)"
fi

echo ""
echo "All tests completed!"
```

Run:
```bash
chmod +x test_phase1.sh
./test_phase1.sh
```

### Performance Testing

#### Basic Load Test with ApacheBench
```bash
# Install if needed
sudo apt install apache2-utils

# Test: 1000 requests, 10 concurrent
ab -n 1000 -c 10 http://localhost:8080/hello
```

Expected metrics:
```
Requests per second:    ~500-1000 [#/sec] (depends on hardware)
Time per request:       ~1-2 ms
```

**Note:** Phase 1 is synchronous (single-threaded), so performance is limited.
Later phases will add async I/O and threading for better performance.

### Graceful Shutdown Test
```bash
# Start server
./build/examples/http_server_example

# In another terminal, send SIGINT
pkill -SIGINT http_server_example

# Server should log:
# [info] Received SIGINT, shutting down...
# [info] Stopping server...
# [info] Server stopped
# [info] Server shut down cleanly
```

---

## Code Walkthrough

Let's walk through how a request flows through the system.

### Example Request: `GET /hello HTTP/1.1`

#### Step 1: Client Sends Request
```
GET /hello HTTP/1.1\r\n
Host: localhost:8080\r\n
User-Agent: curl/7.81.0\r\n
Accept: */*\r\n
\r\n
```

#### Step 2: Server Accepts Connection
```cpp
// In HttpServer::serve_forever()
auto client = listener_.accept();  // Blocks until connection arrives
// Returns unique_ptr<Socket> for new client connection
```

**Learning point:** `accept()` creates a new socket for the client.
The listener socket remains open for more connections.

#### Step 3: Read Data from Socket
```cpp
// In HttpServer::read_request()
auto recv_result = socket.receive(4096);  // Read up to 4KB
// Returns vector<uint8_t> with received data
```

#### Step 4: Parse Request (Character by Character)
```cpp
// In HttpParser::parse()
for (size_t i = 0; i < len; ++i) {
    char c = data[i];

    switch (state_) {
        case ParseState::METHOD:
            // Reading 'G', 'E', 'T'
            if (c == ' ') {
                request_.method = HttpMethodUtils::from_string(buffer_); // "GET"
                state_ = ParseState::URL;
            } else {
                buffer_ += c;
            }
            break;

        case ParseState::URL:
            // Reading '/', 'h', 'e', 'l', 'l', 'o'
            if (c == ' ') {
                request_.url = buffer_; // "/hello"
                state_ = ParseState::VERSION;
            } else {
                buffer_ += c;
            }
            break;

        // ... more states ...
    }
}
```

**State transitions:**
1. METHOD: `"GET"` → space → next state
2. URL: `"/hello"` → space → next state
3. VERSION: `"HTTP/1.1"` → CRLF → next state
4. HEADER_NAME: `"Host"` → colon → next state
5. HEADER_VALUE: `"localhost:8080"` → CRLF → back to HEADER_NAME
6. HEADER_NAME: empty line (CRLF) → COMPLETE

#### Step 5: Call User Handler
```cpp
// In http_server_example.cpp
HttpResponse handle_request(const HttpRequest& request) {
    if (request.url == "/hello" && request.method == HttpMethod::GET) {
        HttpResponse response(HttpStatus::OK);
        response.set_body("Hello from DFS HTTP Server!\n");
        response.set_header("Content-Type", "text/plain");
        return response;
    }
    // ... other routes ...
}
```

#### Step 6: Serialize Response
```cpp
// In HttpResponse::serialize()
std::vector<uint8_t> HttpResponse::serialize() const {
    std::ostringstream oss;

    // Status line
    oss << "HTTP/1.1 " << status_code << " " << reason_phrase << "\r\n";

    // Headers
    for (const auto& [name, value] : headers) {
        oss << name << ": " << value << "\r\n";
    }

    // Empty line
    oss << "\r\n";

    // Convert to bytes
    std::string str = oss.str();
    std::vector<uint8_t> result(str.begin(), str.end());

    // Append body
    result.insert(result.end(), body.begin(), body.end());

    return result;
}

// Produces:
// HTTP/1.1 200 OK\r\n
// Content-Type: text/plain\r\n
// Content-Length: 31\r\n
// \r\n
// Hello from DFS HTTP Server!\n
```

#### Step 7: Send Response
```cpp
// In HttpServer::send_response()
std::vector<uint8_t> data = response.serialize();
socket.send(data);  // Send all bytes over network
```

#### Step 8: Close Connection
```cpp
client->close();  // Close client socket
// Socket destructor ensures proper cleanup (RAII)
```

### Data Structures at Each Step

```cpp
// After parsing:
HttpRequest {
    method: HttpMethod::GET,
    url: "/hello",
    version: HttpVersion::HTTP_1_1,
    headers: {
        {"Host", "localhost:8080"},
        {"User-Agent", "curl/7.81.0"},
        {"Accept", "*/*"}
    },
    body: []  // empty
}

// After handler:
HttpResponse {
    version: HttpVersion::HTTP_1_1,
    status_code: 200,
    reason_phrase: "OK",
    headers: {
        {"Content-Type", "text/plain"},
        {"Content-Length", "31"}
    },
    body: [72, 101, 108, 108, 111, ...]  // "Hello from DFS..."
}
```

---

## Learning Points

### 1. State Machines for Protocol Parsing

**Why state machines?**
- Data arrives incrementally (network chunks)
- Need to pause and resume parsing
- Explicit error handling at each state
- Memory efficient (no need to buffer entire request)

**Key insight:** HTTP parsing can't use simple string splitting because:
- Headers span multiple lines
- Body length is determined by header (Content-Length)
- Need to handle malformed requests gracefully

### 2. RAII for Resource Management

**Socket ownership:**
```cpp
std::unique_ptr<Socket> client = listener_.accept();
// When unique_ptr goes out of scope, Socket destructor is called
// Destructor automatically closes the socket
// No manual cleanup needed!
```

**Why this matters:**
- Prevents resource leaks
- Exception-safe (socket closes even if handler throws)
- Clear ownership semantics

### 3. Error Handling with Result<T>

**Instead of exceptions:**
```cpp
Result<void> listen(uint16_t port) {
    if (/* error */) {
        return Err<void>("Failed to bind");
    }
    return Ok();
}

// Usage:
auto result = server.listen(8080);
if (result.is_error()) {
    // Must handle error explicitly
    spdlog::error("{}", result.error());
}
```

**Benefits:**
- Errors are visible in function signatures
- Can't accidentally ignore errors
- No hidden control flow (unlike exceptions)
- Better for systems programming

### 4. Function Objects for Flexibility

**Handler as std::function:**
```cpp
using HttpRequestHandler = std::function<HttpResponse(const HttpRequest&)>;

// Can use lambda:
server.set_handler([](const HttpRequest& req) {
    return HttpResponse(HttpStatus::OK);
});

// Or free function:
server.set_handler(my_handler_function);

// Or member function:
server.set_handler(std::bind(&MyClass::handler, &obj, std::placeholders::_1));
```

### 5. Binary-Safe Data Handling

**Why std::vector<uint8_t> for body?**
```cpp
std::vector<uint8_t> body;  // Not std::string!
```

Reasons:
- Can handle binary data (images, PDFs, etc.)
- No issues with null bytes ('\0')
- Explicit about being bytes, not text
- Can convert to string when needed: `std::string(body.begin(), body.end())`

### 6. Incremental Parsing Pattern

**Parser doesn't need all data at once:**
```cpp
HttpParser parser;

// Data arrives in chunks
parser.parse(chunk1, size1);  // Returns false (need more data)
parser.parse(chunk2, size2);  // Returns false (need more data)
parser.parse(chunk3, size3);  // Returns true (complete!)

HttpRequest request = parser.get_request();
```

This pattern is crucial for:
- Network programming (data arrives in packets)
- Large file handling (stream processing)
- Memory efficiency (don't buffer entire input)

---

## How This Contributes

### To the Overall System

Phase 1 HTTP implementation serves as the **control plane foundation**:

```
┌─────────────────────────────────────────────┐
│          Distributed File Sync System       │
├─────────────────────────────────────────────┤
│                                             │
│  ┌─────────────┐         ┌─────────────┐   │
│  │   Client    │◄───────►│   Server    │   │
│  │             │         │             │   │
│  │  ┌───────┐  │  HTTP   │  ┌───────┐  │   │  ← Phase 1
│  │  │  HTTP │  │ Control │  │  HTTP │  │   │    (Control Plane)
│  │  │Handler│  │  Plane  │  │Server │  │   │
│  │  └───┬───┘  │         │  └───┬───┘  │   │
│  │      │      │         │      │      │   │
│  │  ┌───▼───┐  │         │  ┌───▼───┐  │   │
│  │  │ Sync  │  │         │  │ Sync  │  │   │  ← Phase 4
│  │  │Engine │  │         │  │Engine │  │   │    (Data Plane)
│  │  └───┬───┘  │         │  └───┬───┘  │   │
│  │      │      │         │      │      │   │
│  │  ┌───▼───┐  │         │  ┌───▼───┐  │   │
│  │  │ Meta  │  │         │  │ Meta  │  │   │  ← Phase 2
│  │  │ Store │  │         │  │ Store │  │   │    (Metadata)
│  │  └───────┘  │         │  └───────┘  │   │
│  └─────────────┘         └─────────────┘   │
└─────────────────────────────────────────────┘
```

### Integration Points

**Phase 1 → Phase 2 (Metadata):**
- HTTP endpoints to send/receive file metadata
- POST /api/metadata - Upload file metadata
- GET /api/metadata/:file_id - Retrieve metadata

**Phase 1 → Phase 4 (Sync):**
- HTTP endpoints for sync coordination
- POST /api/sync/start - Begin sync session
- GET /api/sync/status - Check sync status
- POST /api/sync/diff - Exchange file diffs

**Phase 1 → Phase 6 (Production):**
- HTTP endpoints for monitoring
- GET /health - Health check
- GET /metrics - Prometheus metrics
- POST /admin/reload - Reload configuration

---

## Why This Is Crucial

### 1. Foundation for Distributed Communication

**Without HTTP:**
- Would need custom protocol (more work)
- Harder to debug (no curl/browser)
- No standardized tools
- Steeper learning curve

**With HTTP:**
- Universal protocol (everyone knows it)
- Standard tools (curl, Postman, browsers)
- Easy debugging (human-readable)
- Can use existing infrastructure (load balancers, proxies)

### 2. Understanding Protocol Implementation

**Key skills learned:**
- How to parse binary protocols
- State machine design
- Error handling in parsers
- Network data handling

**Transferable to:**
- Database protocols (MySQL, PostgreSQL)
- Messaging protocols (MQTT, AMQP)
- Custom protocols (game servers, IoT)

### 3. Real-World Systems Programming

**Production patterns:**
- Incremental parsing (memory efficient)
- Error propagation (Result<T> type)
- Resource management (RAII)
- Separation of concerns (parser vs server vs handler)

### 4. Building Block for Complex Features

**Future capabilities enabled:**
- REST APIs (file operations, user management)
- WebSocket upgrade (for real-time updates)
- Streaming responses (large file downloads)
- Chunked transfer encoding (unknown length)
- Compression (gzip, deflate)
- Authentication (Basic, Bearer tokens)

---

## Next Steps

### Immediate Improvements (Still Phase 1)

1. **Add More HTTP Methods**
   ```cpp
   // In http_types.hpp
   enum class HttpMethod {
       // ... existing ...
       PATCH,
       TRACE,
       CONNECT
   };
   ```

2. **Implement Keep-Alive**
   ```cpp
   // In HttpServer::handle_connection()
   std::string connection = request.get_header("Connection");
   bool keep_alive = (connection == "keep-alive");

   if (keep_alive) {
       // Don't close connection, handle next request
   }
   ```

3. **Add Request Timeouts**
   ```cpp
   socket.set_timeout(30);  // 30 second timeout
   ```

4. **Better Error Messages**
   ```cpp
   // Include request details in error responses
   response.set_body(
       "Error: " + error_message + "\n" +
       "Request: " + request.method + " " + request.url
   );
   ```

### Moving to Phase 2: Metadata & DDL

**What's next:**
- Design metadata format (DDL language)
- Implement parser (lexer + parser)
- Binary serialization
- HTTP endpoints for metadata exchange

**Integration:**
```cpp
// New endpoints in http_server_example.cpp
if (request.url == "/api/metadata" && request.method == HttpMethod::POST) {
    // Parse DDL from request body
    auto metadata = DDLParser::parse(request.body_as_string());

    // Store metadata
    metadata_store.save(metadata);

    return HttpResponse(HttpStatus::CREATED);
}
```

### Testing Checklist

Before moving to Phase 2, ensure:
- [ ] Server starts and listens on specified port
- [ ] Can handle GET requests
- [ ] Can handle POST requests with body
- [ ] Correctly parses HTTP headers
- [ ] Returns appropriate status codes
- [ ] Handles malformed requests gracefully
- [ ] Logs all requests
- [ ] Shuts down cleanly on SIGINT
- [ ] No memory leaks (test with valgrind)
- [ ] Works on both Linux and Windows

### Performance Baseline

Record these metrics for comparison with later phases:
```bash
# Requests per second (single connection)
ab -n 1000 -c 1 http://localhost:8080/hello

# Requests per second (10 concurrent)
ab -n 1000 -c 10 http://localhost:8080/hello

# Memory usage
ps aux | grep http_server_example

# CPU usage under load
top -p $(pgrep http_server_example)
```

---

## Exercises for Deeper Learning

### Exercise 1: Add URL Query Parameters
Extend the parser to extract query parameters from URLs.

```cpp
// Input: "/search?q=test&page=2"
// Output: {
//   path: "/search",
//   params: {{"q", "test"}, {"page", "2"}}
// }
```

**Hint:** Parse '?' and '&' characters in URL state.

### Exercise 2: Implement Chunked Transfer Encoding
Support responses with unknown length.

```cpp
// Response:
// Transfer-Encoding: chunked
//
// 5\r\n
// Hello\r\n
// 7\r\n
// , World\r\n
// 0\r\n
// \r\n
```

**Hint:** Add CHUNK_SIZE and CHUNK_DATA states to parser.

### Exercise 3: Add Request Routing
Create a routing system like Express.js.

```cpp
Router router;
router.get("/users/:id", [](const HttpRequest& req) {
    std::string id = req.params["id"];
    return HttpResponse(HttpStatus::OK);
});
```

**Hint:** Use regex or simple pattern matching.

### Exercise 4: WebSocket Upgrade
Implement WebSocket handshake.

```cpp
// Client sends:
// GET /chat HTTP/1.1
// Upgrade: websocket
// Connection: Upgrade
// Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==

// Server responds:
// HTTP/1.1 101 Switching Protocols
// Upgrade: websocket
// Connection: Upgrade
// Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
```

**Hint:** Check Upgrade header, compute accept hash.

### Exercise 5: Multi-threaded Server
Handle connections in separate threads.

```cpp
while (running_) {
    auto client = listener_.accept();

    std::thread([this, client = std::move(client)]() mutable {
        handle_connection(std::move(client));
    }).detach();
}
```

**Hint:** Use thread pool to limit concurrent threads.

---

## Additional Resources

### HTTP/1.1 Specifications
- [RFC 7230](https://tools.ietf.org/html/rfc7230) - Message Syntax and Routing
- [RFC 7231](https://tools.ietf.org/html/rfc7231) - Semantics and Content
- [RFC 7232](https://tools.ietf.org/html/rfc7232) - Conditional Requests
- [RFC 7233](https://tools.ietf.org/html/rfc7233) - Range Requests
- [RFC 7234](https://tools.ietf.org/html/rfc7234) - Caching
- [RFC 7235](https://tools.ietf.org/html/rfc7235) - Authentication

### Related Reading
- [High Performance Browser Networking](https://hpbn.co/) - Chapters on HTTP
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/) - Socket programming
- [HTTP: The Definitive Guide](https://www.oreilly.com/library/view/http-the-definitive/1565925092/)

### Tools
- **curl** - Command-line HTTP client
- **Postman** - GUI HTTP client
- **Wireshark** - Network packet analyzer
- **ApacheBench (ab)** - HTTP load testing
- **wrk** - Modern HTTP benchmarking tool

---

## Conclusion

Phase 1 is complete! We've built a working HTTP/1.1 server from scratch, learning:
- Protocol parsing with state machines
- Network programming patterns
- Error handling strategies
- Modern C++ practices (RAII, Result<T>, std::function)

This server will serve as the control plane for the entire distributed file sync system, handling:
- Client registration
- Sync coordination
- Metadata exchange
- Status monitoring
- Admin operations

**Next:** Phase 2 - Metadata & DDL System

---

**Last Updated:** 2025-09-29
**Phase Status:** ✅ Complete
**Next Phase:** Phase 2 - Metadata & DDL System