# Phase 2 Reference - Metadata & DDL System

## Overview

**Phase:** 2 - Metadata & DDL System
**Duration:** 6-8 days
**Difficulty:** Medium-Hard
**Prerequisites:** Phase 1 complete (HTTP server + router)

---

## Table of Contents

1. [Big Picture: Why Metadata?](#big-picture-why-metadata)
2. [What We're Building](#what-were-building)
3. [Task Breakdown](#task-breakdown)
4. [Task 1: Design Metadata Format](#task-1-design-metadata-format)
5. [Task 2: Build the Lexer (Tokenizer)](#task-2-build-the-lexer-tokenizer)
6. [Task 3: Build the Parser](#task-3-build-the-parser)
7. [Task 4: Metadata Storage](#task-4-metadata-storage)
8. [Task 5: Binary Serialization](#task-5-binary-serialization)
9. [Task 6: HTTP API Integration](#task-6-http-api-integration)
10. [Task 7: Testing & Validation](#task-7-testing--validation)
11. [Learning Objectives](#learning-objectives)
12. [How This Fits Into The System](#how-this-fits-into-the-system)
13. [Code Organization](#code-organization)
14. [Common Pitfalls](#common-pitfalls)

---

## Big Picture: Why Metadata?

### The Problem

Imagine you have a file `document.pdf` on your laptop and want to sync it to the server.

**Without Metadata (Naive Approach):**
```
Laptop → Server: "Do you have document.pdf?"
Server: "I don't know. Send it to me so I can check."
Laptop: *uploads 100MB file*
Server: "Oh, I already had this exact file!"
❌ Result: Wasted 100MB bandwidth + time
```

**With Metadata (Smart Approach):**
```
Laptop → Server: "Do you have document.pdf? My hash: abc123"
Server → Laptop: "Yes! I have hash abc123 too."
Laptop: "Great, no sync needed!"
✅ Result: Saved 100MB! Only exchanged ~100 bytes!
```

### What is Metadata?

**Metadata = "Data about data"**

Instead of storing/transferring the actual file, we store **information about the file**:

```
File: document.pdf (100 MB on disk)
  ↓
Metadata: (only a few KB)
  - name: "document.pdf"
  - size: 104857600 bytes
  - hash: "sha256:abc123..."  ← Fingerprint of file content
  - last_modified: "2024-01-15 10:30:00"
  - version: 5
  - owner: "laptop1"
```

**Why is this powerful?**
- **Fast comparison:** Compare hashes (32 bytes) instead of entire files (MB/GB)
- **Bandwidth saving:** Only sync when hashes differ
- **Version tracking:** Know which version each node has
- **Conflict detection:** See if two nodes modified same file

---

## What We're Building

### Components Overview

```
┌─────────────────────────────────────────────────────────┐
│                    Phase 2 System                       │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  ┌──────────────────────────────────────────────────┐  │
│  │  1. DDL Parser                                   │  │
│  │     Input:  Text file with metadata              │  │
│  │     Output: FileMetadata struct                  │  │
│  │                                                   │  │
│  │     file_metadata:                               │  │
│  │       name: "doc.pdf"      →   FileMetadata {   │  │
│  │       version: 3                   name: "doc.pdf"│ │
│  │       hash: "abc123"               version: 3    │  │
│  │                                    hash: "abc123"│  │
│  │                                }                 │  │
│  └──────────────────────────────────────────────────┘  │
│                         ↓                               │
│  ┌──────────────────────────────────────────────────┐  │
│  │  2. Metadata Store (In-Memory Database)          │  │
│  │                                                   │  │
│  │     store.save(metadata);                        │  │
│  │     auto result = store.get("doc.pdf");         │  │
│  │     auto all = store.list_all();                 │  │
│  │                                                   │  │
│  │     Storage:                                      │  │
│  │     unordered_map<string, FileMetadata>          │  │
│  │       "doc.pdf"    → {version: 3, hash: "abc"}  │  │
│  │       "image.jpg"  → {version: 1, hash: "def"}  │  │
│  └──────────────────────────────────────────────────┘  │
│                         ↓                               │
│  ┌──────────────────────────────────────────────────┐  │
│  │  3. Binary Serializer                            │  │
│  │                                                   │  │
│  │     FileMetadata struct → bytes for network      │  │
│  │                                                   │  │
│  │     serialize():   struct → [01 02 03 04 ...]   │  │
│  │     deserialize(): [bytes] → struct             │  │
│  │                                                   │  │
│  │     Why? Efficient network transfer              │  │
│  │     Text JSON: ~500 bytes                        │  │
│  │     Binary:    ~100 bytes  ✅                    │  │
│  └──────────────────────────────────────────────────┘  │
│                         ↓                               │
│  ┌──────────────────────────────────────────────────┐  │
│  │  4. HTTP API Endpoints (Using Phase 1 Router!)  │  │
│  │                                                   │  │
│  │     POST /api/register                           │  │
│  │       → Register new client node                 │  │
│  │                                                   │  │
│  │     POST /api/metadata/update                    │  │
│  │       → Client sends metadata                    │  │
│  │                                                   │  │
│  │     GET /api/metadata/:filename                  │  │
│  │       → Get metadata for specific file           │  │
│  │                                                   │  │
│  │     GET /api/sync/diff                           │  │
│  │       → Compare metadata, return differences     │  │
│  └──────────────────────────────────────────────────┘  │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

---

## Task Breakdown

### Overview of Tasks

| Task | Component | Duration | Difficulty |
|------|-----------|----------|------------|
| **Task 1** | Design metadata format | 0.5 day | Easy |
| **Task 2** | Build lexer (tokenizer) | 1-2 days | Medium |
| **Task 3** | Build parser | 1-2 days | Medium-Hard |
| **Task 4** | Metadata storage | 1 day | Easy-Medium |
| **Task 5** | Binary serialization | 1 day | Medium |
| **Task 6** | HTTP API integration | 1-2 days | Medium |
| **Task 7** | Testing & validation | 1 day | Easy-Medium |

**Total:** 6-8 days

---

## Task 1: Design Metadata Format

**Duration:** 0.5 day
**Difficulty:** Easy
**Goal:** Define what information we need about files

### What You'll Do

Design a text format (DDL - Domain-Specific Language) for describing file metadata.

### Why This First?

You need to know WHAT data you're parsing before you can build a parser!

### Example DDL Format

```yaml
# Simple YAML-like syntax
file_metadata:
  name: "document.pdf"
  version: 3
  size: 1048576
  hash: "sha256:abc123def456..."
  last_modified: "2024-01-15T10:30:00Z"
  owner: "laptop1"
  permissions:
    owner: "rw"
    group: "r"
    other: "-"
  sync_state:
    status: "synced"
    replicas:
      - node: "server1"
        version: 3
        synced_at: "2024-01-15T10:35:00Z"
      - node: "desktop1"
        version: 2
        synced_at: "2024-01-14T09:20:00Z"
```

### Design Decisions

**1. Why YAML-like syntax?**
- Human-readable (easy to debug)
- Simple to parse (indentation-based)
- Familiar to developers
- No complex grammar

**2. What fields do we need?**

| Field | Why? | Example |
|-------|------|---------|
| `name` | Identify the file | `"document.pdf"` |
| `version` | Track changes over time | `3` (third version) |
| `size` | Quick size check | `1048576` (1 MB) |
| `hash` | Content fingerprint | `"sha256:abc123..."` |
| `last_modified` | When was it changed | `"2024-01-15T10:30:00Z"` |
| `owner` | Who owns this file | `"laptop1"` |
| `sync_state` | Replication status | See which nodes have it |

**3. Why nested structure?**
- Group related data (permissions together)
- Extensible (can add more fields later)
- Matches how we think about files

### Deliverable

Create file: `docs/metadata_format_spec.md`

Document:
- DDL syntax rules
- All supported fields
- Examples of valid metadata
- Edge cases (missing fields, invalid values)

### Code to Write

```cpp
// include/dfs/metadata/types.hpp
namespace dfs {
namespace metadata {

struct FilePermissions {
    std::string owner;  // "rw", "r", "-"
    std::string group;
    std::string other;
};

struct ReplicaInfo {
    std::string node;           // Node ID like "server1"
    int version;                // Version number at this node
    std::string synced_at;      // ISO 8601 timestamp
};

struct SyncState {
    std::string status;                   // "synced", "pending", "conflict"
    std::vector<ReplicaInfo> replicas;    // Where is this file?
};

struct FileMetadata {
    std::string name;
    int version;
    size_t size;
    std::string hash;
    std::string last_modified;
    std::string owner;
    FilePermissions permissions;
    SyncState sync_state;

    // Helper methods
    bool needs_sync(const FileMetadata& other) const;
    bool is_newer_than(const FileMetadata& other) const;
};

} // namespace metadata
} // namespace dfs
```

**Why these structs?**
- `FileMetadata`: Main structure - represents ONE file's metadata
- `FilePermissions`: Group permission fields (could add more later)
- `ReplicaInfo`: Track where file exists and what version
- `SyncState`: Group sync-related information

---

## Task 2: Build the Lexer (Tokenizer)

**Duration:** 1-2 days
**Difficulty:** Medium
**Goal:** Convert text → tokens

### What is a Lexer?

**Lexer (Tokenizer)** = Breaks text into meaningful pieces (tokens)

**Example:**
```
Input text:  "file_metadata:\n  name: \"doc.pdf\""
              ↓ Lexer
Output tokens:
  Token(IDENTIFIER, "file_metadata")
  Token(COLON, ":")
  Token(NEWLINE, "\n")
  Token(INDENT, 2 spaces)
  Token(IDENTIFIER, "name")
  Token(COLON, ":")
  Token(STRING, "doc.pdf")
```

### Why Do We Need This?

You can't parse text directly. You need to:
1. **Lexer:** Text → Tokens (recognize "words")
2. **Parser:** Tokens → Data Structure (understand "meaning")

**Analogy:**
- **Lexer** = Reading individual words in a sentence
- **Parser** = Understanding what the sentence means

### Token Types

```cpp
// include/dfs/metadata/lexer.hpp
enum class TokenType {
    // Identifiers and literals
    IDENTIFIER,      // file_metadata, name, version
    STRING,          // "document.pdf"
    NUMBER,          // 123, 3.14

    // Structural
    COLON,           // :
    DASH,            // -
    NEWLINE,         // \n
    INDENT,          // Leading spaces (2, 4, 6...)
    DEDENT,          // Decrease in indentation

    // Special
    END_OF_FILE,     // Reached end
    INVALID          // Error token
};

struct Token {
    TokenType type;
    std::string value;
    size_t line;
    size_t column;

    Token(TokenType t, std::string v, size_t l, size_t c)
        : type(t), value(v), line(l), column(c) {}
};
```

**Why track line/column?**
- Error messages: "Error at line 5, column 12"
- Debugging: Know exactly where problem is

### Lexer State Machine

```cpp
class Lexer {
private:
    std::string input_;
    size_t position_;
    size_t line_;
    size_t column_;
    int current_indent_;
    std::stack<int> indent_stack_;

public:
    explicit Lexer(std::string input);
    Result<Token> next_token();

private:
    char peek() const;
    char advance();
    void skip_whitespace();
    bool is_at_end() const;

    Result<Token> read_identifier();
    Result<Token> read_string();
    Result<Token> read_number();
    Result<Token> handle_indentation();
};
```

### Implementation Strategy

**Step 1: Read one character at a time**
```cpp
Result<Token> Lexer::next_token() {
    skip_whitespace();

    if (is_at_end()) {
        return Ok(Token(TokenType::END_OF_FILE, "", line_, column_));
    }

    char c = peek();

    // Check what character we have
    if (std::isalpha(c) || c == '_') {
        return read_identifier();
    }
    if (c == '"') {
        return read_string();
    }
    if (std::isdigit(c)) {
        return read_number();
    }
    if (c == ':') {
        advance();
        return Ok(Token(TokenType::COLON, ":", line_, column_));
    }
    // ... more cases
}
```

**Why this approach?**
- Look at first character, decide what token it is
- Read until token ends
- Return the token

**Step 2: Handle indentation (tricky!)**
```cpp
Result<Token> Lexer::handle_indentation() {
    // Count leading spaces
    int spaces = 0;
    while (peek() == ' ') {
        spaces++;
        advance();
    }

    if (spaces > current_indent_) {
        // Indentation increased
        indent_stack_.push(current_indent_);
        current_indent_ = spaces;
        return Ok(Token(TokenType::INDENT, "", line_, column_));
    }
    else if (spaces < current_indent_) {
        // Indentation decreased
        current_indent_ = spaces;
        return Ok(Token(TokenType::DEDENT, "", line_, column_));
    }

    // Same indentation - no token
    return next_token();  // Continue to actual content
}
```

**Why is indentation hard?**
- YAML-like syntax uses indentation for structure
- Need to track indent level changes
- One indent increase might = multiple dedents

### Testing the Lexer

```cpp
TEST(Lexer, TokenizesSimpleMetadata) {
    std::string input = R"(
file_metadata:
  name: "test.txt"
  version: 1
)";

    Lexer lexer(input);

    // Expected tokens
    auto tok1 = lexer.next_token();  // IDENTIFIER "file_metadata"
    EXPECT_EQ(tok1.value().type, TokenType::IDENTIFIER);

    auto tok2 = lexer.next_token();  // COLON
    EXPECT_EQ(tok2.value().type, TokenType::COLON);

    auto tok3 = lexer.next_token();  // NEWLINE
    EXPECT_EQ(tok3.value().type, TokenType::NEWLINE);

    auto tok4 = lexer.next_token();  // INDENT
    EXPECT_EQ(tok4.value().type, TokenType::INDENT);

    // ... verify all tokens
}
```

### Deliverables

1. **`include/dfs/metadata/lexer.hpp`** - Lexer interface
2. **`src/metadata/lexer.cpp`** - Lexer implementation
3. **`tests/metadata/lexer_test.cpp`** - Unit tests

### Learning Points

**State machines:**
- Lexer maintains state (position, line, column, indent level)
- Transitions between states based on input

**Character classification:**
```cpp
std::isalpha(c)  // Letter?
std::isdigit(c)  // Number?
std::isspace(c)  // Whitespace?
```

**String building:**
```cpp
std::string identifier;
while (std::isalnum(peek()) || peek() == '_') {
    identifier += advance();
}
```

---

## Task 3: Build the Parser

**Duration:** 1-2 days
**Difficulty:** Medium-Hard
**Goal:** Convert tokens → FileMetadata struct

### What is a Parser?

**Parser** = Takes tokens from lexer, understands their meaning, builds data structures

```
Lexer output:   [IDENTIFIER, COLON, INDENT, IDENTIFIER, STRING, ...]
                 ↓ Parser
Parser output:  FileMetadata { name: "doc.pdf", version: 3, ... }
```

### Parsing Technique: Recursive Descent

**Recursive Descent** = Each grammar rule becomes a function

**Grammar (informal):**
```
metadata       → "file_metadata" ":" NEWLINE INDENT fields DEDENT
fields         → field (NEWLINE field)*
field          → IDENTIFIER ":" value
value          → STRING | NUMBER | nested_object
nested_object  → NEWLINE INDENT fields DEDENT
```

**Each rule = one function:**
```cpp
class DDLParser {
public:
    Result<FileMetadata> parse_metadata();

private:
    Lexer lexer_;
    Token current_token_;

    Result<void> parse_fields(FileMetadata& meta);
    Result<void> parse_field(FileMetadata& meta);
    Result<std::string> parse_value();

    Result<void> expect(TokenType type);
    Result<void> advance();
};
```

### Parser Implementation

**Step 1: Main parse function**
```cpp
Result<FileMetadata> DDLParser::parse_metadata() {
    FileMetadata meta;

    // Expect: "file_metadata"
    auto result = expect(TokenType::IDENTIFIER);
    if (result.is_error()) {
        return Err<FileMetadata>("Expected 'file_metadata'");
    }

    if (current_token_.value != "file_metadata") {
        return Err<FileMetadata>("Expected 'file_metadata', got: " + current_token_.value);
    }

    // Expect: ":"
    advance();
    result = expect(TokenType::COLON);
    if (result.is_error()) {
        return Err<FileMetadata>("Expected ':' after 'file_metadata'");
    }

    // Expect: NEWLINE
    advance();
    result = expect(TokenType::NEWLINE);
    if (result.is_error()) {
        return Err<FileMetadata>("Expected newline");
    }

    // Expect: INDENT
    advance();
    result = expect(TokenType::INDENT);
    if (result.is_error()) {
        return Err<FileMetadata>("Expected indentation");
    }

    // Parse all fields
    advance();
    auto fields_result = parse_fields(meta);
    if (fields_result.is_error()) {
        return Err<FileMetadata>(fields_result.error());
    }

    return Ok(meta);
}
```

**Why so much error checking?**
- User might provide invalid DDL
- Need helpful error messages
- Parser should never crash

**Step 2: Parse fields**
```cpp
Result<void> DDLParser::parse_fields(FileMetadata& meta) {
    while (current_token_.type != TokenType::DEDENT &&
           current_token_.type != TokenType::END_OF_FILE) {

        auto result = parse_field(meta);
        if (result.is_error()) {
            return result;
        }

        advance();
    }

    return Ok();
}
```

**Step 3: Parse individual field**
```cpp
Result<void> DDLParser::parse_field(FileMetadata& meta) {
    // Get field name
    if (current_token_.type != TokenType::IDENTIFIER) {
        return Err<void>("Expected field name");
    }

    std::string field_name = current_token_.value;

    // Expect ':'
    advance();
    if (current_token_.type != TokenType::COLON) {
        return Err<void>("Expected ':' after field name");
    }

    // Get value
    advance();

    // Assign to correct field
    if (field_name == "name") {
        if (current_token_.type != TokenType::STRING) {
            return Err<void>("Expected string for 'name'");
        }
        meta.name = current_token_.value;
    }
    else if (field_name == "version") {
        if (current_token_.type != TokenType::NUMBER) {
            return Err<void>("Expected number for 'version'");
        }
        meta.version = std::stoi(current_token_.value);
    }
    else if (field_name == "size") {
        if (current_token_.type != TokenType::NUMBER) {
            return Err<void>("Expected number for 'size'");
        }
        meta.size = std::stoull(current_token_.value);
    }
    // ... more fields

    return Ok();
}
```

**Why this pattern?**
- Each field type has specific expectations
- Type checking at parse time
- Store directly in FileMetadata struct

### Handling Nested Objects

```yaml
permissions:
  owner: "rw"
  group: "r"
```

**Parse nested structure:**
```cpp
Result<void> DDLParser::parse_permissions(FileMetadata& meta) {
    // Expect NEWLINE
    auto result = expect(TokenType::NEWLINE);
    if (result.is_error()) return result;

    // Expect INDENT
    advance();
    result = expect(TokenType::INDENT);
    if (result.is_error()) return result;

    // Parse nested fields
    advance();
    while (current_token_.type != TokenType::DEDENT) {
        if (current_token_.value == "owner") {
            advance();  // Skip ':'
            advance();  // Get value
            meta.permissions.owner = current_token_.value;
        }
        // ... more nested fields

        advance();
    }

    return Ok();
}
```

### Testing the Parser

```cpp
TEST(DDLParser, ParsesCompleteMetadata) {
    std::string ddl = R"(
file_metadata:
  name: "document.pdf"
  version: 3
  size: 1048576
  hash: "sha256:abc123"
)";

    DDLParser parser(ddl);
    auto result = parser.parse_metadata();

    ASSERT_TRUE(result.is_ok());

    FileMetadata meta = result.value();
    EXPECT_EQ(meta.name, "document.pdf");
    EXPECT_EQ(meta.version, 3);
    EXPECT_EQ(meta.size, 1048576);
    EXPECT_EQ(meta.hash, "sha256:abc123");
}

TEST(DDLParser, ReportsErrorOnInvalidSyntax) {
    std::string ddl = "file_metadata\n  name";  // Missing colon

    DDLParser parser(ddl);
    auto result = parser.parse_metadata();

    ASSERT_TRUE(result.is_error());
    EXPECT_THAT(result.error(), testing::HasSubstr("Expected ':'"));
}
```

### Deliverables

1. **`include/dfs/metadata/parser.hpp`** - Parser interface
2. **`src/metadata/parser.cpp`** - Parser implementation
3. **`tests/metadata/parser_test.cpp`** - Unit tests with error cases

### Learning Points

**Recursive descent:**
- Top-down parsing approach
- Each grammar rule = function
- Easy to understand and implement

**Error recovery:**
- Continue parsing after error (find multiple errors)
- Provide line/column information
- Give helpful suggestions

**Type checking:**
- Validate field types during parsing
- Catch errors early (before runtime)

---

## Task 4: Metadata Storage

**Duration:** 1 day
**Difficulty:** Easy-Medium
**Goal:** Store and retrieve metadata efficiently

### What is Metadata Storage?

**In-memory database** for FileMetadata objects

```cpp
MetadataStore store;

// Save metadata
store.save(metadata);

// Retrieve metadata
auto result = store.get("document.pdf");

// List all files
auto all = store.list_all();

// Check if file exists
bool exists = store.has("document.pdf");
```

### Why Do We Need This?

- **Server** needs to remember metadata for all files
- **Client** needs to track local files
- **Fast lookups** by filename
- **Persistence** (save to disk, load on startup)

### Storage Strategy

**In-memory:** Use `std::unordered_map` (hash table)

```cpp
class MetadataStore {
private:
    // Primary storage: filename → metadata
    std::unordered_map<std::string, FileMetadata> metadata_map_;

    // Optional: Secondary index for fast hash lookups
    std::unordered_map<std::string, std::string> hash_to_filename_;

    // Thread safety
    mutable std::shared_mutex mutex_;

public:
    // Save or update metadata
    void save(const FileMetadata& metadata);

    // Retrieve metadata
    Result<FileMetadata> get(const std::string& filename) const;

    // Check existence
    bool has(const std::string& filename) const;

    // List all
    std::vector<FileMetadata> list_all() const;

    // Remove
    bool remove(const std::string& filename);

    // Persistence
    Result<void> save_to_disk(const std::string& path) const;
    Result<void> load_from_disk(const std::string& path);

    // Statistics
    size_t size() const;
    size_t total_size() const;  // Sum of all file sizes
};
```

**Why `unordered_map`?**
- O(1) average lookup time
- O(1) insertion
- Perfect for filename → metadata mapping

**Why `shared_mutex`?**
- Multiple threads can READ simultaneously
- Only one thread can WRITE at a time
- Better performance than regular mutex

### Implementation

**Save metadata:**
```cpp
void MetadataStore::save(const FileMetadata& metadata) {
    std::unique_lock lock(mutex_);  // Exclusive lock (writing)

    // Update or insert
    metadata_map_[metadata.name] = metadata;

    // Update secondary index
    hash_to_filename_[metadata.hash] = metadata.name;

    spdlog::debug("Saved metadata for: {}", metadata.name);
}
```

**Get metadata:**
```cpp
Result<FileMetadata> MetadataStore::get(const std::string& filename) const {
    std::shared_lock lock(mutex_);  // Shared lock (reading)

    auto it = metadata_map_.find(filename);
    if (it == metadata_map_.end()) {
        return Err<FileMetadata>("File not found: " + filename);
    }

    return Ok(it->second);
}
```

**List all metadata:**
```cpp
std::vector<FileMetadata> MetadataStore::list_all() const {
    std::shared_lock lock(mutex_);

    std::vector<FileMetadata> result;
    result.reserve(metadata_map_.size());

    for (const auto& [filename, metadata] : metadata_map_) {
        result.push_back(metadata);
    }

    return result;
}
```

**Why reserve()?**
- Pre-allocate vector space
- Avoid reallocation during loop
- Performance optimization

### Persistence: Save to Disk

**Format:** Use your binary serializer (Task 5)

```cpp
Result<void> MetadataStore::save_to_disk(const std::string& path) const {
    std::shared_lock lock(mutex_);

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return Err<void>("Failed to open file: " + path);
    }

    // Write count
    size_t count = metadata_map_.size();
    file.write(reinterpret_cast<const char*>(&count), sizeof(count));

    // Write each metadata
    MetadataSerializer serializer;
    for (const auto& [filename, metadata] : metadata_map_) {
        auto bytes = serializer.serialize(metadata);

        // Write size
        size_t size = bytes.size();
        file.write(reinterpret_cast<const char*>(&size), sizeof(size));

        // Write data
        file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    spdlog::info("Saved {} metadata entries to: {}", count, path);
    return Ok();
}
```

**Load from disk:**
```cpp
Result<void> MetadataStore::load_from_disk(const std::string& path) {
    std::unique_lock lock(mutex_);

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Err<void>("Failed to open file: " + path);
    }

    // Read count
    size_t count;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));

    // Read each metadata
    MetadataSerializer serializer;
    for (size_t i = 0; i < count; ++i) {
        // Read size
        size_t size;
        file.read(reinterpret_cast<char*>(&size), sizeof(size));

        // Read data
        std::vector<uint8_t> bytes(size);
        file.read(reinterpret_cast<char*>(bytes.data()), bytes.size());

        // Deserialize
        auto result = serializer.deserialize(bytes);
        if (result.is_error()) {
            return Err<void>("Deserialization failed: " + result.error());
        }

        // Store
        metadata_map_[result.value().name] = result.value();
    }

    spdlog::info("Loaded {} metadata entries from: {}", count, path);
    return Ok();
}
```

### Testing

```cpp
TEST(MetadataStore, SaveAndRetrieve) {
    MetadataStore store;

    FileMetadata meta;
    meta.name = "test.txt";
    meta.version = 1;
    meta.hash = "abc123";

    store.save(meta);

    auto result = store.get("test.txt");
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().name, "test.txt");
    EXPECT_EQ(result.value().hash, "abc123");
}

TEST(MetadataStore, Persistence) {
    MetadataStore store1;

    // Add metadata
    FileMetadata meta;
    meta.name = "test.txt";
    meta.version = 1;
    store1.save(meta);

    // Save to disk
    store1.save_to_disk("/tmp/metadata.db");

    // Load in new store
    MetadataStore store2;
    auto result = store2.load_from_disk("/tmp/metadata.db");
    ASSERT_TRUE(result.is_ok());

    // Verify
    auto retrieved = store2.get("test.txt");
    ASSERT_TRUE(retrieved.is_ok());
    EXPECT_EQ(retrieved.value().name, "test.txt");
}
```

### Deliverables

1. **`include/dfs/metadata/store.hpp`** - MetadataStore interface
2. **`src/metadata/store.cpp`** - Implementation
3. **`tests/metadata/store_test.cpp`** - Unit tests

### Learning Points

**Hash tables:**
- O(1) lookup is incredibly fast
- Trade-off: memory usage
- Perfect for filename → data mapping

**Reader-writer locks:**
```cpp
std::shared_mutex mutex_;
std::shared_lock lock(mutex_);   // Multiple readers
std::unique_lock lock(mutex_);   // Single writer
```

**Persistence patterns:**
- Save: Serialize each object → write to file
- Load: Read from file → deserialize each object
- Transaction-safe (write to temp, rename)

---

## Task 5: Binary Serialization

**Duration:** 1 day
**Difficulty:** Medium
**Goal:** Convert FileMetadata ↔ bytes efficiently

### Why Binary Serialization?

**Problem:** Need to send metadata over network

**Option 1: JSON (Text)**
```json
{
  "name": "document.pdf",
  "version": 3,
  "size": 1048576,
  "hash": "sha256:abc123..."
}
```
**Size:** ~200 bytes
**Pros:** Human-readable, easy to debug
**Cons:** Larger, slower to parse

**Option 2: Binary (Custom Format)**
```
[01 00 00 00] [0C 00] [64 6F 63 75 6D 65 6E 74 2E 70 64 66] [03 00 00 00] ...
```
**Size:** ~80 bytes
**Pros:** Compact, fast to parse
**Cons:** Not human-readable

**We'll use binary for production, but design for debuggability!**

### Binary Format Design

**Layout:**
```
┌─────────────────────────────────────────────────────────┐
│ Header (8 bytes)                                        │
│   - Magic number (4 bytes): 0x44465301 ("DFS\x01")     │
│   - Version (4 bytes): Schema version                   │
├─────────────────────────────────────────────────────────┤
│ Body                                                    │
│   - name (length-prefixed string)                      │
│   - version (4 bytes)                                   │
│   - size (8 bytes)                                      │
│   - hash (length-prefixed string)                      │
│   - last_modified (length-prefixed string)             │
│   - owner (length-prefixed string)                     │
│   - ... (nested structs)                               │
└─────────────────────────────────────────────────────────┘
```

**Length-prefixed string:**
```
[02 00] [41 42]
  ↑       ↑
  length  "AB"
  (2)
```

### Implementation

```cpp
class MetadataSerializer {
public:
    static constexpr uint32_t MAGIC_NUMBER = 0x44465301;  // "DFS\x01"
    static constexpr uint32_t SCHEMA_VERSION = 1;

    std::vector<uint8_t> serialize(const FileMetadata& metadata);
    Result<FileMetadata> deserialize(const std::vector<uint8_t>& data);

private:
    // Write helpers
    void write_uint32(std::vector<uint8_t>& buffer, uint32_t value);
    void write_uint64(std::vector<uint8_t>& buffer, uint64_t value);
    void write_string(std::vector<uint8_t>& buffer, const std::string& str);

    // Read helpers
    uint32_t read_uint32(const uint8_t* data, size_t& offset);
    uint64_t read_uint64(const uint8_t* data, size_t& offset);
    std::string read_string(const uint8_t* data, size_t& offset);
};
```

**Serialize:**
```cpp
std::vector<uint8_t> MetadataSerializer::serialize(const FileMetadata& metadata) {
    std::vector<uint8_t> buffer;

    // Header
    write_uint32(buffer, MAGIC_NUMBER);
    write_uint32(buffer, SCHEMA_VERSION);

    // Body
    write_string(buffer, metadata.name);
    write_uint32(buffer, metadata.version);
    write_uint64(buffer, metadata.size);
    write_string(buffer, metadata.hash);
    write_string(buffer, metadata.last_modified);
    write_string(buffer, metadata.owner);

    // Nested: permissions
    write_string(buffer, metadata.permissions.owner);
    write_string(buffer, metadata.permissions.group);
    write_string(buffer, metadata.permissions.other);

    // Nested: sync_state
    write_string(buffer, metadata.sync_state.status);
    write_uint32(buffer, metadata.sync_state.replicas.size());
    for (const auto& replica : metadata.sync_state.replicas) {
        write_string(buffer, replica.node);
        write_uint32(buffer, replica.version);
        write_string(buffer, replica.synced_at);
    }

    return buffer;
}
```

**Helper: Write uint32**
```cpp
void MetadataSerializer::write_uint32(std::vector<uint8_t>& buffer, uint32_t value) {
    buffer.push_back(static_cast<uint8_t>(value & 0xFF));
    buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}
```

**Why this byte order?**
- Little-endian (least significant byte first)
- Matches x86/x64 architecture
- Most common for binary formats

**Helper: Write string**
```cpp
void MetadataSerializer::write_string(std::vector<uint8_t>& buffer, const std::string& str) {
    // Write length (2 bytes, max 65535 chars)
    uint16_t length = static_cast<uint16_t>(str.length());
    buffer.push_back(static_cast<uint8_t>(length & 0xFF));
    buffer.push_back(static_cast<uint8_t>((length >> 8) & 0xFF));

    // Write string data
    buffer.insert(buffer.end(), str.begin(), str.end());
}
```

**Deserialize:**
```cpp
Result<FileMetadata> MetadataSerializer::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < 8) {
        return Err<FileMetadata>("Data too short");
    }

    size_t offset = 0;

    // Header
    uint32_t magic = read_uint32(data.data(), offset);
    if (magic != MAGIC_NUMBER) {
        return Err<FileMetadata>("Invalid magic number");
    }

    uint32_t version = read_uint32(data.data(), offset);
    if (version != SCHEMA_VERSION) {
        return Err<FileMetadata>("Unsupported schema version");
    }

    // Body
    FileMetadata metadata;
    metadata.name = read_string(data.data(), offset);
    metadata.version = read_uint32(data.data(), offset);
    metadata.size = read_uint64(data.data(), offset);
    metadata.hash = read_string(data.data(), offset);
    metadata.last_modified = read_string(data.data(), offset);
    metadata.owner = read_string(data.data(), offset);

    // Nested: permissions
    metadata.permissions.owner = read_string(data.data(), offset);
    metadata.permissions.group = read_string(data.data(), offset);
    metadata.permissions.other = read_string(data.data(), offset);

    // Nested: sync_state
    metadata.sync_state.status = read_string(data.data(), offset);
    uint32_t replica_count = read_uint32(data.data(), offset);
    for (uint32_t i = 0; i < replica_count; ++i) {
        ReplicaInfo replica;
        replica.node = read_string(data.data(), offset);
        replica.version = read_uint32(data.data(), offset);
        replica.synced_at = read_string(data.data(), offset);
        metadata.sync_state.replicas.push_back(replica);
    }

    return Ok(metadata);
}
```

**Helper: Read uint32**
```cpp
uint32_t MetadataSerializer::read_uint32(const uint8_t* data, size_t& offset) {
    uint32_t value =
        static_cast<uint32_t>(data[offset]) |
        (static_cast<uint32_t>(data[offset + 1]) << 8) |
        (static_cast<uint32_t>(data[offset + 2]) << 16) |
        (static_cast<uint32_t>(data[offset + 3]) << 24);
    offset += 4;
    return value;
}
```

### Testing

```cpp
TEST(Serializer, RoundTrip) {
    FileMetadata original;
    original.name = "test.txt";
    original.version = 5;
    original.size = 1024;
    original.hash = "abc123";

    MetadataSerializer serializer;

    // Serialize
    auto bytes = serializer.serialize(original);

    // Deserialize
    auto result = serializer.deserialize(bytes);
    ASSERT_TRUE(result.is_ok());

    // Verify
    FileMetadata deserialized = result.value();
    EXPECT_EQ(deserialized.name, original.name);
    EXPECT_EQ(deserialized.version, original.version);
    EXPECT_EQ(deserialized.size, original.size);
    EXPECT_EQ(deserialized.hash, original.hash);
}

TEST(Serializer, DetectsCorruption) {
    std::vector<uint8_t> invalid_data = {0x01, 0x02, 0x03};

    MetadataSerializer serializer;
    auto result = serializer.deserialize(invalid_data);

    ASSERT_TRUE(result.is_error());
    EXPECT_THAT(result.error(), testing::HasSubstr("Invalid magic"));
}
```

### Deliverables

1. **`include/dfs/metadata/serializer.hpp`** - Serializer interface
2. **`src/metadata/serializer.cpp`** - Implementation
3. **`tests/metadata/serializer_test.cpp`** - Tests including corruption

### Learning Points

**Binary formats:**
- Efficient but requires careful design
- Endianness matters (little vs big)
- Need magic numbers for validation

**Schema evolution:**
- Version field allows format changes
- Can support multiple versions simultaneously
- Read old format, write new format

**Bounds checking:**
```cpp
if (offset + 4 > data.size()) {
    return Err<FileMetadata>("Unexpected end of data");
}
```

---

## Task 6: HTTP API Integration

**Duration:** 1-2 days
**Difficulty:** Medium
**Goal:** Connect metadata system to HTTP server

### Endpoints to Implement

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/register` | POST | Register new client node |
| `/api/metadata/update` | POST | Client sends metadata |
| `/api/metadata/get/:filename` | GET | Get metadata for file |
| `/api/metadata/list` | GET | List all metadata |
| `/api/sync/diff` | POST | Compare metadata, get diff |

### Implementation Strategy

**Use Phase 1 Router + MetadataStore!**

```cpp
// In main.cpp or server setup
MetadataStore metadata_store;

HttpRouter router;

// Register endpoints
router.post("/api/register", [&metadata_store](const HttpContext& ctx) {
    return handle_register(ctx, metadata_store);
});

router.post("/api/metadata/update", [&metadata_store](const HttpContext& ctx) {
    return handle_metadata_update(ctx, metadata_store);
});

router.get("/api/metadata/get/:filename", [&metadata_store](const HttpContext& ctx) {
    return handle_metadata_get(ctx, metadata_store);
});

router.get("/api/metadata/list", [&metadata_store](const HttpContext& ctx) {
    return handle_metadata_list(ctx, metadata_store);
});

router.post("/api/sync/diff", [&metadata_store](const HttpContext& ctx) {
    return handle_sync_diff(ctx, metadata_store);
});
```

### Endpoint 1: Register Client

**Purpose:** Client registers with server, gets unique ID

**Request:**
```json
POST /api/register
{
  "client_name": "laptop1",
  "platform": "linux"
}
```

**Response:**
```json
{
  "status": "registered",
  "client_id": "laptop1-1234567890",
  "message": "Client registered successfully"
}
```

**Implementation:**
```cpp
HttpResponse handle_register(const HttpContext& ctx, MetadataStore& store) {
    // Parse JSON body
    std::string body = ctx.request.body_as_string();
    json request_json = json::parse(body);

    std::string client_name = request_json["client_name"];
    std::string platform = request_json["platform"];

    // Generate unique ID
    std::string client_id = client_name + "-" + std::to_string(std::time(nullptr));

    spdlog::info("Registered new client: {}", client_id);

    // Create response
    json response_json = {
        {"status", "registered"},
        {"client_id", client_id},
        {"message", "Client registered successfully"}
    };

    HttpResponse response(HttpStatus::CREATED);
    response.set_body(response_json.dump(2));
    response.set_header("Content-Type", "application/json");
    return response;
}
```

### Endpoint 2: Update Metadata

**Purpose:** Client sends file metadata to server

**Request:**
```json
POST /api/metadata/update
{
  "filename": "document.pdf",
  "version": 3,
  "size": 1048576,
  "hash": "sha256:abc123...",
  "last_modified": "2024-01-15T10:30:00Z"
}
```

**Response:**
```json
{
  "status": "updated",
  "message": "Metadata stored successfully"
}
```

**Implementation:**
```cpp
HttpResponse handle_metadata_update(const HttpContext& ctx, MetadataStore& store) {
    std::string body = ctx.request.body_as_string();
    json request_json = json::parse(body);

    // Build FileMetadata from JSON
    FileMetadata metadata;
    metadata.name = request_json["filename"];
    metadata.version = request_json["version"];
    metadata.size = request_json["size"];
    metadata.hash = request_json["hash"];
    metadata.last_modified = request_json["last_modified"];

    // Save to store
    store.save(metadata);

    spdlog::info("Updated metadata for: {}", metadata.name);

    json response_json = {
        {"status", "updated"},
        {"message", "Metadata stored successfully"}
    };

    HttpResponse response(HttpStatus::OK);
    response.set_body(response_json.dump(2));
    response.set_header("Content-Type", "application/json");
    return response;
}
```

### Endpoint 3: Get Metadata

**Purpose:** Retrieve metadata for specific file

**Request:**
```
GET /api/metadata/get/document.pdf
```

**Response:**
```json
{
  "filename": "document.pdf",
  "version": 3,
  "size": 1048576,
  "hash": "sha256:abc123...",
  "last_modified": "2024-01-15T10:30:00Z"
}
```

**Implementation:**
```cpp
HttpResponse handle_metadata_get(const HttpContext& ctx, MetadataStore& store) {
    // Extract filename from URL parameter
    std::string filename = ctx.get_param("filename");

    // Get from store
    auto result = store.get(filename);

    if (result.is_error()) {
        json error_json = {
            {"error", "not_found"},
            {"message", "File not found: " + filename}
        };

        HttpResponse response(HttpStatus::NOT_FOUND);
        response.set_body(error_json.dump(2));
        response.set_header("Content-Type", "application/json");
        return response;
    }

    // Convert metadata to JSON
    FileMetadata metadata = result.value();
    json response_json = {
        {"filename", metadata.name},
        {"version", metadata.version},
        {"size", metadata.size},
        {"hash", metadata.hash},
        {"last_modified", metadata.last_modified}
    };

    HttpResponse response(HttpStatus::OK);
    response.set_body(response_json.dump(2));
    response.set_header("Content-Type", "application/json");
    return response;
}
```

### Endpoint 4: List All Metadata

**Purpose:** Get list of all tracked files

**Request:**
```
GET /api/metadata/list
```

**Response:**
```json
{
  "count": 2,
  "files": [
    {
      "filename": "document.pdf",
      "version": 3,
      "hash": "sha256:abc123..."
    },
    {
      "filename": "image.jpg",
      "version": 1,
      "hash": "sha256:def456..."
    }
  ]
}
```

**Implementation:**
```cpp
HttpResponse handle_metadata_list(const HttpContext& ctx, MetadataStore& store) {
    auto all_metadata = store.list_all();

    json files_array = json::array();
    for (const auto& metadata : all_metadata) {
        files_array.push_back({
            {"filename", metadata.name},
            {"version", metadata.version},
            {"hash", metadata.hash}
        });
    }

    json response_json = {
        {"count", all_metadata.size()},
        {"files", files_array}
    };

    HttpResponse response(HttpStatus::OK);
    response.set_body(response_json.dump(2));
    response.set_header("Content-Type", "application/json");
    return response;
}
```

### Endpoint 5: Sync Diff (Most Important!)

**Purpose:** Client sends its metadata, server compares, returns what needs syncing

**Request:**
```json
POST /api/sync/diff
{
  "client_files": [
    {
      "filename": "document.pdf",
      "version": 2,
      "hash": "sha256:old123"
    },
    {
      "filename": "newfile.txt",
      "version": 1,
      "hash": "sha256:new456"
    }
  ]
}
```

**Response:**
```json
{
  "need_upload": [
    "newfile.txt"
  ],
  "need_download": [
    {
      "filename": "document.pdf",
      "version": 3,
      "hash": "sha256:abc123"
    }
  ],
  "up_to_date": []
}
```

**Implementation:**
```cpp
HttpResponse handle_sync_diff(const HttpContext& ctx, MetadataStore& store) {
    std::string body = ctx.request.body_as_string();
    json request_json = json::parse(body);

    json need_upload = json::array();
    json need_download = json::array();
    json up_to_date = json::array();

    // Compare each client file with server
    for (const auto& client_file : request_json["client_files"]) {
        std::string filename = client_file["filename"];
        std::string client_hash = client_file["hash"];
        int client_version = client_file["version"];

        auto server_result = store.get(filename);

        if (server_result.is_error()) {
            // Server doesn't have this file
            need_upload.push_back(filename);
        } else {
            FileMetadata server_meta = server_result.value();

            if (server_meta.hash != client_hash) {
                // Different content
                if (server_meta.version > client_version) {
                    // Server has newer version
                    need_download.push_back({
                        {"filename", filename},
                        {"version", server_meta.version},
                        {"hash", server_meta.hash}
                    });
                } else {
                    // Client has newer version
                    need_upload.push_back(filename);
                }
            } else {
                // Same content
                up_to_date.push_back(filename);
            }
        }
    }

    json response_json = {
        {"need_upload", need_upload},
        {"need_download", need_download},
        {"up_to_date", up_to_date}
    };

    HttpResponse response(HttpStatus::OK);
    response.set_body(response_json.dump(2));
    response.set_header("Content-Type", "application/json");
    return response;
}
```

**Why this is powerful:**
- Client sends ONLY metadata (KB), not files (MB/GB)
- Server compares hashes instantly
- Client knows exactly what to upload/download
- Efficient synchronization!

### Testing the API

```bash
# Register client
curl -X POST http://localhost:8080/api/register \
  -d '{"client_name":"laptop1","platform":"linux"}'

# Update metadata
curl -X POST http://localhost:8080/api/metadata/update \
  -d '{"filename":"test.txt","version":1,"size":1024,"hash":"abc123","last_modified":"2024-01-15T10:00:00Z"}'

# Get metadata
curl http://localhost:8080/api/metadata/get/test.txt

# List all
curl http://localhost:8080/api/metadata/list

# Sync diff
curl -X POST http://localhost:8080/api/sync/diff \
  -d '{"client_files":[{"filename":"test.txt","version":1,"hash":"abc123"}]}'
```

### Deliverables

1. **`src/server/metadata_handlers.cpp`** - All endpoint handlers
2. **`src/server/main.cpp`** - Server with router + metadata store
3. **Integration tests** - Test full request/response cycle

### Learning Points

**REST API design:**
- GET for reading
- POST for creating/updating
- Use HTTP status codes correctly
- Return JSON for structured data

**State management:**
- MetadataStore is shared across all requests
- Thread-safe (shared_mutex)
- Lambdas capture by reference

**JSON serialization:**
```cpp
// C++ → JSON
json j = {{"key", value}};

// JSON → C++
std::string str = j["key"];
```

---

## Task 7: Testing & Validation

**Duration:** 1 day
**Difficulty:** Easy-Medium
**Goal:** Ensure everything works together

### Unit Tests

Already covered in individual tasks, but ensure:
- Lexer tests (all token types)
- Parser tests (valid + invalid DDL)
- Store tests (CRUD operations)
- Serializer tests (round-trip, corruption)

### Integration Tests

**Test 1: DDL → Metadata → Storage**
```cpp
TEST(Integration, DDLToStorage) {
    std::string ddl = R"(
file_metadata:
  name: "test.txt"
  version: 1
  size: 1024
  hash: "abc123"
)";

    // Parse DDL
    DDLParser parser(ddl);
    auto parse_result = parser.parse_metadata();
    ASSERT_TRUE(parse_result.is_ok());

    // Store metadata
    MetadataStore store;
    store.save(parse_result.value());

    // Retrieve
    auto get_result = store.get("test.txt");
    ASSERT_TRUE(get_result.is_ok());
    EXPECT_EQ(get_result.value().hash, "abc123");
}
```

**Test 2: Metadata → Serialization → Network → Deserialization**
```cpp
TEST(Integration, SerializationRoundTrip) {
    FileMetadata original;
    original.name = "test.txt";
    original.version = 3;

    // Serialize
    MetadataSerializer serializer;
    auto bytes = serializer.serialize(original);

    // Simulate network transfer (no-op)

    // Deserialize
    auto result = serializer.deserialize(bytes);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().name, original.name);
}
```

**Test 3: HTTP API End-to-End**
```cpp
TEST(Integration, HTTPWorkflow) {
    // Start server
    MetadataStore store;
    HttpRouter router;
    setup_metadata_endpoints(router, store);

    HttpServer server(4);
    server.set_handler([&router](const HttpRequest& req) {
        return router.handle_request(req);
    });

    // Simulate request
    HttpRequest request;
    request.method = HttpMethod::POST;
    request.url = "/api/metadata/update";
    request.set_body(R"({
        "filename": "test.txt",
        "version": 1,
        "size": 1024,
        "hash": "abc123"
    })");

    // Get response
    HttpResponse response = router.handle_request(request);

    // Verify
    EXPECT_EQ(response.status_code, 200);

    // Verify stored
    auto result = store.get("test.txt");
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().hash, "abc123");
}
```

### Manual Testing

**Test with real server + curl:**

```bash
# Start server
./build/src/server/dfs_server --port 8080

# In another terminal:
# 1. Register
curl -X POST http://localhost:8080/api/register \
  -d '{"client_name":"laptop1","platform":"linux"}'

# 2. Add metadata
curl -X POST http://localhost:8080/api/metadata/update \
  -d '{"filename":"doc.pdf","version":1,"size":1024,"hash":"abc123","last_modified":"2024-01-15T10:00:00Z"}'

# 3. Get metadata
curl http://localhost:8080/api/metadata/get/doc.pdf

# 4. List all
curl http://localhost:8080/api/metadata/list

# 5. Test sync diff
curl -X POST http://localhost:8080/api/sync/diff \
  -d '{"client_files":[{"filename":"doc.pdf","version":1,"hash":"abc123"}]}'
```

### Performance Testing

```bash
# Benchmark metadata operations
ab -n 10000 -c 100 http://localhost:8080/api/metadata/list

# Expected:
# - Requests per second: 10,000+
# - Time per request: <1ms
```

### Deliverables

1. **Comprehensive test suite** (all components)
2. **Integration tests** (components working together)
3. **Performance benchmarks** (baseline metrics)
4. **Manual test script** (bash script with curl commands)

---

## Learning Objectives

### What You'll Learn

**1. Language Design & Parsing**
- How to design a custom syntax
- Lexical analysis (tokenization)
- Recursive descent parsing
- Error reporting with line/column info

**2. Data Structures**
- Hash maps for fast lookups
- In-memory databases
- Indexing strategies
- Reader-writer locks for concurrency

**3. Binary Protocols**
- Why binary > text for network transfer
- Endianness (little-endian vs big-endian)
- Schema versioning
- Backward compatibility

**4. API Design**
- RESTful endpoints
- JSON request/response
- Error handling in APIs
- State management in servers

**5. System Integration**
- Connecting multiple components
- Dependency injection (passing store to handlers)
- Testing strategies (unit, integration, manual)

---

## How This Fits Into The System

### Phase 2 in Context

```
┌─────────────────────────────────────────────────────────┐
│              COMPLETE SYSTEM                            │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  Phase 0: Foundation (Socket, Result<T>, RAII)         │
│       ↓                                                 │
│  Phase 1: HTTP Server + Router ✅                      │
│       ↓                                                 │
│  Phase 2: Metadata & DDL ← YOU ARE HERE                │
│       │                                                 │
│       │ Enables:                                        │
│       │  - Fast file comparison (hash checking)        │
│       │  - Efficient sync decisions                    │
│       │  - Version tracking                            │
│       │  - Conflict detection                          │
│       ↓                                                 │
│  Phase 3: Event System                                 │
│       │  (Metadata changes trigger events)             │
│       ↓                                                 │
│  Phase 4: Sync Engine                                  │
│       │  (Uses metadata to decide what to sync)        │
│       ↓                                                 │
│  Phase 5: OS Integration                               │
│       │  (Real-time file changes → metadata updates)   │
│       ↓                                                 │
│  Phase 6: Production                                   │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

### Why Metadata Comes Before Sync

**Without metadata:**
```
Client: "Sync everything"
Server: *transfers 100GB*
Client: "Most files were already in sync!"
❌ Wasted bandwidth
```

**With metadata:**
```
Client: "Here are my file hashes"
Server: "I need these 3 files, you need these 2"
Client: *syncs only 5 files (10MB)*
✅ Efficient!
```

### What Phase 2 Enables

1. **Phase 3 (Events):**
   - FileMetadataChanged event
   - FileVersionConflict event

2. **Phase 4 (Sync Engine):**
   - Compare metadata → decide what to sync
   - Use hashes to detect changes
   - Track versions for conflict resolution

3. **Phase 5 (OS Integration):**
   - File changes → update metadata
   - Metadata changes → trigger sync

---

## Code Organization

### Directory Structure

```
Distributed-File-Sync-System/
├── include/dfs/metadata/
│   ├── types.hpp           # FileMetadata, ReplicaInfo, etc.
│   ├── lexer.hpp           # Lexer class
│   ├── parser.hpp          # DDLParser class
│   ├── store.hpp           # MetadataStore class
│   └── serializer.hpp      # MetadataSerializer class
│
├── src/metadata/
│   ├── CMakeLists.txt      # Build configuration
│   ├── lexer.cpp           # Lexer implementation
│   ├── parser.cpp          # Parser implementation
│   ├── store.cpp           # Store implementation
│   └── serializer.cpp      # Serializer implementation
│
├── src/server/
│   ├── metadata_handlers.cpp  # HTTP endpoint handlers
│   └── main.cpp               # Server with metadata integration
│
├── tests/metadata/
│   ├── lexer_test.cpp
│   ├── parser_test.cpp
│   ├── store_test.cpp
│   ├── serializer_test.cpp
│   └── integration_test.cpp
│
├── docs/
│   ├── phase_2_reference.md      # This file
│   └── metadata_format_spec.md   # DDL specification
│
└── examples/
    └── metadata_example.cpp      # Standalone demo
```

### CMakeLists.txt for metadata module

```cmake
# src/metadata/CMakeLists.txt
add_library(dfs_metadata STATIC
    lexer.cpp
    parser.cpp
    store.cpp
    serializer.cpp
)

target_include_directories(dfs_metadata PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(dfs_metadata PUBLIC
    dfs_core
    spdlog::spdlog
)
```

### Adding to root CMakeLists.txt

```cmake
# Root CMakeLists.txt
add_subdirectory(src/metadata)

# Server depends on metadata
target_link_libraries(dfs_server PRIVATE
    dfs_network
    dfs_metadata
    nlohmann_json::nlohmann_json
)
```

---

## Common Pitfalls

### Pitfall 1: Not Handling Errors

**❌ Bad:**
```cpp
auto metadata = parser.parse_metadata().value();  // CRASH if error!
```

**✅ Good:**
```cpp
auto result = parser.parse_metadata();
if (result.is_error()) {
    spdlog::error("Parse failed: {}", result.error());
    return;
}
auto metadata = result.value();
```

### Pitfall 2: Forgetting Thread Safety

**❌ Bad:**
```cpp
class MetadataStore {
    std::unordered_map<std::string, FileMetadata> map_;

public:
    void save(const FileMetadata& meta) {
        map_[meta.name] = meta;  // ← Race condition!
    }
};
```

**✅ Good:**
```cpp
class MetadataStore {
    std::unordered_map<std::string, FileMetadata> map_;
    mutable std::shared_mutex mutex_;

public:
    void save(const FileMetadata& meta) {
        std::unique_lock lock(mutex_);  // ✅ Thread-safe
        map_[meta.name] = meta;
    }
};
```

### Pitfall 3: Not Validating Input

**❌ Bad:**
```cpp
int version = json["version"];  // CRASH if missing or wrong type!
```

**✅ Good:**
```cpp
if (!json.contains("version") || !json["version"].is_number()) {
    return Err<void>("Missing or invalid 'version' field");
}
int version = json["version"];
```

### Pitfall 4: Inefficient String Operations

**❌ Bad:**
```cpp
std::string result;
for (int i = 0; i < 1000; ++i) {
    result += some_string;  // ← Reallocates 1000 times!
}
```

**✅ Good:**
```cpp
std::string result;
result.reserve(estimated_size);  // ✅ Pre-allocate
for (int i = 0; i < 1000; ++i) {
    result += some_string;
}
```

### Pitfall 5: Memory Leaks with Manual Management

**❌ Bad:**
```cpp
Token* token = new Token(...);
// ... might return early ...
delete token;  // ← Might not be reached!
```

**✅ Good:**
```cpp
auto token = std::make_unique<Token>(...);
// ✅ Automatically deleted when out of scope
```

---

## Summary & Next Steps

### What You Accomplished

After completing Phase 2, you will have:

✅ **A custom DDL language** for describing file metadata
✅ **A lexer** that tokenizes DDL text
✅ **A parser** that builds FileMetadata structs
✅ **A metadata store** (in-memory database)
✅ **Binary serialization** for efficient network transfer
✅ **HTTP API** for metadata operations
✅ **Full integration** with Phase 1 HTTP server + router

### Ready for Phase 3

Phase 2 metadata system enables Phase 3 (Event System):

```cpp
// Phase 3 will use your metadata like this:
void on_file_changed(const std::string& filename) {
    // Update metadata
    auto metadata = compute_metadata(filename);
    metadata_store.save(metadata);

    // Emit event (Phase 3)
    emit(FileMetadataChanged{filename, metadata});

    // Event triggers sync (Phase 4)
}
```

### Testing Checklist

Before moving to Phase 3:
- [ ] Lexer tokenizes all valid DDL
- [ ] Parser handles nested structures
- [ ] Metadata store persists to disk
- [ ] Serializer round-trips all fields
- [ ] HTTP API responds to all endpoints
- [ ] curl commands all work
- [ ] No memory leaks (run with valgrind)
- [ ] Thread-safe under load (test with multiple clients)

### Estimated Completion

**Total time:** 6-8 days if following tasks sequentially

**Parallel approach (faster):**
- Days 1-2: Tasks 1-2 (DDL design + Lexer)
- Days 3-4: Task 3-4 (Parser + Store) - can be done in parallel
- Day 5: Task 5 (Serialization)
- Days 6-7: Task 6 (HTTP integration)
- Day 8: Task 7 (Testing)

**Total with parallel:** 5-7 days

---

## Additional Resources

### Recommended Reading

**Parsing:**
- [Crafting Interpreters](https://craftinginterpreters.com/) - Chapters 4-6
- [Let's Build a Simple Interpreter](https://ruslanspivak.com/lsbasi-part1/)

**Binary Protocols:**
- [Protocol Buffers Design](https://protobuf.dev/)
- [MessagePack Specification](https://msgpack.org/)

**Data Structures:**
- [C++ unordered_map internals](https://en.cppreference.com/w/cpp/container/unordered_map)
- [Reader-writer locks](https://en.cppreference.com/w/cpp/thread/shared_mutex)

### Example Projects

**Similar implementations:**
- [YAML parser in C++](https://github.com/jbeder/yaml-cpp)
- [JSON for Modern C++](https://github.com/nlohmann/json)
- [Cap'n Proto](https://capnproto.org/) - Binary serialization

### Tools

**Debugging:**
```bash
# Memory leaks
valgrind --leak-check=full ./metadata_test

# Parse errors
./metadata_example --verbose file.ddl
```

**Profiling:**
```bash
# CPU profiling
perf record -g ./metadata_benchmark
perf report
```

---

**Good luck with Phase 2!** 🚀

This phase is where you'll learn the most about parsing, data structures, and system integration. Take your time, test thoroughly, and don't hesitate to refer back to this document!

---

**Document Version:** 1.0
**Last Updated:** 2025-01-XX
**Phase Status:** 📋 Planning
