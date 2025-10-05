# Phase 3 Reference - Event System (ECS-Inspired)

## Overview

**Phase:** 3 - Event System & Event-Driven Architecture
**Duration:** 4-6 days
**Difficulty:** Medium-Hard
**Prerequisites:** Phase 1 (HTTP Server) ✅ Complete | Phase 2 (Metadata & DDL) ✅ Complete
**Status:** 📝 Planning

---

## Table of Contents

1. [What We've Built So Far](#what-weve-built-so-far)
2. [The Problem: Why Events?](#the-problem-why-events)
3. [What We're Building in Phase 3](#what-were-building-in-phase-3)
4. [Task Breakdown](#task-breakdown)
5. [Task 1: Design Event System Architecture](#task-1-design-event-system-architecture)
6. [Task 2: Type-Safe Event Bus](#task-2-type-safe-event-bus)
7. [Task 3: Thread-Safe Event Queue](#task-3-thread-safe-event-queue)
8. [Task 4: Component System](#task-4-component-system)
9. [Task 5: Event Filtering & Priorities](#task-5-event-filtering--priorities)
10. [Task 6: Integration with Metadata System](#task-6-integration-with-metadata-system)
11. [Task 7: Testing & Benchmarking](#task-7-testing--benchmarking)
12. [Learning Objectives](#learning-objectives)
13. [Advanced C++ Patterns Used](#advanced-c-patterns-used)
14. [Real-World Applications](#real-world-applications)
15. [Next Steps (Phase 4)](#next-steps-phase-4)

---

## What We've Built So Far

### Phase 1: HTTP Server & Router ✅

```
Client ──HTTP──> Server ──Router──> Handler
                                       │
                                       ▼
                                   Response
```

**What we can do:**
- Parse HTTP/1.1 requests
- Route requests to handlers
- Send HTTP responses
- Handle multiple connections concurrently

### Phase 2: Metadata & DDL System ✅

```
DDL Text ──Lexer──> Tokens ──Parser──> FileMetadata ──Serializer──> Binary
                                             │
                                             ▼
                                      MetadataStore
                                      (Thread-safe)
```

**What we can do:**
- Parse DDL text into FileMetadata objects
- Store metadata in-memory (thread-safe with shared_mutex)
- Serialize metadata to binary format
- Track file hashes, sizes, timestamps, replicas
- Expose metadata via HTTP endpoints

**HTTP Endpoints:**
- `POST /metadata/add` - Add new file metadata
- `GET /metadata/get/:path` - Get metadata
- `GET /metadata/list` - List all metadata
- `PUT /metadata/update` - Update metadata
- `DELETE /metadata/delete/:path` - Delete metadata

---

## The Problem: Why Events?

### Current Architecture: Tightly Coupled

Right now, our system is **tightly coupled**. When something happens, code has to directly call other code:

```cpp
// Current approach (Phase 2)
HttpResponse handle_add_metadata(const HttpContext& ctx) {
    // Parse metadata
    auto metadata = parser.parse(ctx.body);

    // Directly call store
    store.add(metadata);

    // What if we want to:
    // - Log this action?
    // - Notify other services?
    // - Update a cache?
    // - Send to analytics?

    // We'd have to modify this function every time!
    logger.log("Added: " + metadata.file_path);       // Adding coupling
    cache.invalidate(metadata.file_path);            // More coupling
    analytics.track("file_added", metadata);         // Even more coupling

    return response;
}
```

**Problems:**
1. **Hard to extend** - Add new feature = modify existing code
2. **Hard to test** - Everything depends on everything
3. **Not scalable** - Adding features makes code messy
4. **Violates Single Responsibility** - Handler does too much

### Event-Driven Architecture: Loosely Coupled

With events, components don't know about each other:

```cpp
// Event-driven approach (Phase 3)
HttpResponse handle_add_metadata(const HttpContext& ctx) {
    // Parse metadata
    auto metadata = parser.parse(ctx.body);

    // Add to store
    store.add(metadata);

    // Just emit event - don't care who listens!
    event_bus.emit(FileAddedEvent{metadata});

    return response;
}

// Elsewhere, components subscribe independently:

// Logger component
event_bus.subscribe<FileAddedEvent>([](const FileAddedEvent& e) {
    spdlog::info("File added: {}", e.metadata.file_path);
});

// Cache component
event_bus.subscribe<FileAddedEvent>([&cache](const FileAddedEvent& e) {
    cache.invalidate(e.metadata.file_path);
});

// Analytics component
event_bus.subscribe<FileAddedEvent>([&analytics](const FileAddedEvent& e) {
    analytics.track("file_added", e.metadata);
});

// Future: Add sync manager without touching handle_add_metadata!
event_bus.subscribe<FileAddedEvent>([&sync](const FileAddedEvent& e) {
    sync.queue_for_upload(e.metadata);
});
```

**Benefits:**
- ✅ **Extensible** - Add new listeners without changing emitters
- ✅ **Testable** - Mock events, test components in isolation
- ✅ **Clean** - Each component has one job
- ✅ **Flexible** - Enable/disable features dynamically

---

## What We're Building in Phase 3

### Component Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    Phase 3 Event System                     │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌────────────────────────────────────────────────────┐    │
│  │              Event Bus (Core)                      │    │
│  │  - Type-safe event dispatch                       │    │
│  │  - Subscribe/Emit operations                      │    │
│  │  - Thread-safe event queue                        │    │
│  │  - Event filtering & priorities                   │    │
│  └──────────┬─────────────────────────┬───────────────┘    │
│             │                         │                     │
│      emit   │                         │   notify            │
│             ▼                         ▼                     │
│  ┌─────────────────┐       ┌──────────────────┐           │
│  │   Emitters      │       │    Subscribers   │           │
│  │                 │       │                  │           │
│  │ - HTTP Handler  │       │ - Logger         │           │
│  │ - File Watcher  │       │ - SyncManager    │           │
│  │ - User Input    │       │ - MetadataStore  │           │
│  │ - Timer         │       │ - Cache          │           │
│  └─────────────────┘       └──────────────────┘           │
│                                                             │
│  ┌────────────────────────────────────────────────────┐    │
│  │          Thread-Safe Event Queue                   │    │
│  │  - Bounded/Unbounded queue                        │    │
│  │  - Priority queue support                         │    │
│  │  - Async processing                               │    │
│  └────────────────────────────────────────────────────┘    │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### Event Types We'll Support

```cpp
// File events
struct FileAddedEvent {
    FileMetadata metadata;
    std::string source;  // "http", "watcher", "sync"
};

struct FileModifiedEvent {
    std::string file_path;
    std::string old_hash;
    std::string new_hash;
    size_t new_size;
};

struct FileDeletedEvent {
    std::string file_path;
    FileMetadata last_metadata;
};

// Sync events
struct SyncStartedEvent {
    std::string node_id;
    size_t file_count;
};

struct SyncCompletedEvent {
    std::string node_id;
    size_t files_synced;
    std::chrono::milliseconds duration;
};

struct SyncFailedEvent {
    std::string node_id;
    std::string error_message;
};

// System events
struct ServerStartedEvent {
    uint16_t port;
};

struct ServerShuttingDownEvent {
    std::string reason;
};
```

### Key Features

1. **Type-Safe Events**
   - Compiler checks event types
   - Can't subscribe to wrong events
   - Autocomplete in IDE

2. **Decoupled Components**
   - Components don't know about each other
   - Add/remove components without changes
   - Plugin-like architecture

3. **Thread-Safe**
   - Emit from any thread
   - Concurrent subscribers
   - Lock-free where possible

4. **Flexible Processing**
   - Sync or async dispatch
   - Event priorities
   - Event filtering

---

## Task Breakdown

```
Phase 3 Tasks (4-6 days)

Day 1: Task 1 + Start Task 2
  ✓ Design event architecture
  ✓ Define event types
  ✓ Start implementing EventBus

Day 2: Task 2 + Task 3
  ✓ Complete type-safe EventBus
  ✓ Template metaprogramming
  ✓ Thread-safe event queue

Day 3: Task 4
  ✓ Component system
  ✓ Subscribe/unsubscribe
  ✓ Component lifecycle

Day 4: Task 5
  ✓ Event filtering
  ✓ Priority queues
  ✓ Async processing

Day 5: Task 6
  ✓ Integrate with metadata system
  ✓ Update HTTP handlers
  ✓ Add logging component

Day 6: Task 7
  ✓ Comprehensive testing
  ✓ Performance benchmarks
  ✓ Documentation
```

---

## Task 1: Design Event System Architecture

### Goal
Understand event-driven patterns and design our event system.

### Event System Patterns

#### Pattern 1: Observer Pattern (Gang of Four)

```cpp
// Classic observer pattern
class Observer {
public:
    virtual void update(const Event& event) = 0;
};

class Subject {
    std::vector<Observer*> observers_;
public:
    void attach(Observer* obs) { observers_.push_back(obs); }
    void notify(const Event& event) {
        for (auto* obs : observers_) {
            obs->update(event);
        }
    }
};
```

**Problems:**
- Not type-safe (all events are same type)
- Must inherit from Observer
- Hard to use with lambdas

#### Pattern 2: Signal/Slot (Qt-style)

```cpp
// Signal/slot pattern
class Signal {
    std::vector<std::function<void(int, std::string)>> slots_;
public:
    void connect(std::function<void(int, std::string)> slot) {
        slots_.push_back(slot);
    }

    void emit(int value, std::string msg) {
        for (auto& slot : slots_) {
            slot(value, msg);
        }
    }
};

// Usage
Signal signal;
signal.connect([](int x, std::string s) {
    std::cout << x << ": " << s << "\n";
});
signal.emit(42, "hello");
```

**Better, but:**
- Still not flexible (fixed signature per signal)
- Can't have multiple event types easily

#### Pattern 3: Type-Safe Event Bus (Modern C++) ⭐

```cpp
// Our approach - type-safe, flexible
class EventBus {
public:
    template<typename EventType>
    void subscribe(std::function<void(const EventType&)> handler);

    template<typename EventType>
    void emit(const EventType& event);
};

// Usage - fully type-safe!
EventBus bus;

bus.subscribe<FileAddedEvent>([](const FileAddedEvent& e) {
    std::cout << "File added: " << e.metadata.file_path << "\n";
});

bus.subscribe<FileDeletedEvent>([](const FileDeletedEvent& e) {
    std::cout << "File deleted: " << e.file_path << "\n";
});

bus.emit(FileAddedEvent{metadata});
bus.emit(FileDeletedEvent{"/test.txt"});
```

**Advantages:**
- ✅ Type-safe (compiler checks)
- ✅ Flexible (any event type)
- ✅ Clean syntax (lambdas work)
- ✅ No base class required

### Design Decisions

#### Decision 1: Sync vs Async Dispatch

**Synchronous (simple):**
```cpp
bus.emit(event);  // Blocks until all handlers done
// Continues here after all handlers complete
```

**Asynchronous (complex but scalable):**
```cpp
bus.emit_async(event);  // Returns immediately
// Handlers run in background
```

**Our choice:** Start with sync, add async later.

#### Decision 2: Event Ownership

**Copy events (simple):**
```cpp
void emit(const EventType& event) {
    for (auto& handler : handlers) {
        handler(event);  // Each handler gets a copy
    }
}
```

**Move events (efficient):**
```cpp
void emit(EventType&& event) {
    for (auto& handler : handlers) {
        handler(std::move(event));  // Move to first, copy to rest
    }
}
```

**Our choice:** Copy for simplicity. Events are small.

#### Decision 3: Thread Safety

Options:
1. **No locks** - User must synchronize
2. **One lock** - Protect entire bus
3. **Per-type locks** - Lock only specific event handlers
4. **Lock-free** - Use atomics

**Our choice:** Per-type locks (good balance).

---

## Task 2: Type-Safe Event Bus

### Goal
Implement a type-safe event bus using template metaprogramming.

### The Challenge: Type Erasure

**Problem:**
```cpp
// We want to store different event types in one container:
std::vector<???> handlers;  // What type???

// We need to store:
std::function<void(const FileAddedEvent&)>
std::function<void(const FileDeletedEvent&)>
std::function<void(const SyncStartedEvent&)>
// ... all different types!
```

**Solution: Type Erasure**

```cpp
// Base class (type erased)
struct HandlerBase {
    virtual ~HandlerBase() = default;
    virtual void call(const void* event) = 0;
};

// Derived template (typed)
template<typename EventType>
struct HandlerImpl : HandlerBase {
    std::function<void(const EventType&)> func;

    HandlerImpl(std::function<void(const EventType&)> f) : func(f) {}

    void call(const void* event) override {
        // Cast back to concrete type
        const EventType* typed_event = static_cast<const EventType*>(event);
        func(*typed_event);
    }
};

// Now we can store all handlers!
std::unordered_map<std::type_index, std::vector<std::unique_ptr<HandlerBase>>> handlers_;
```

### Implementation

#### Header File: `include/dfs/events/event_bus.hpp`

```cpp
/**
 * @file event_bus.hpp
 * @brief Type-safe event bus for decoupled component communication
 *
 * WHY THIS FILE EXISTS:
 * Enables loose coupling between components. Components emit events
 * without knowing who will handle them, and subscribe to events
 * without knowing who emits them.
 *
 * WHAT IT DOES:
 * - Type-safe event subscription and emission
 * - Thread-safe concurrent access
 * - Support for any event type
 * - Handler registration and unregistration
 *
 * HOW IT WORKS:
 * Uses template metaprogramming and type erasure to store
 * handlers for different event types in a single container.
 *
 * EXAMPLE:
 * EventBus bus;
 * bus.subscribe<MyEvent>([](const MyEvent& e) { ... });
 * bus.emit(MyEvent{...});
 */

#pragma once

#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <shared_mutex>

namespace dfs::events {

/**
 * @brief Type-safe event bus
 *
 * Uses template metaprogramming to provide compile-time type safety
 * while supporting any event type at runtime.
 *
 * THREAD SAFETY:
 * - Multiple threads can emit events concurrently
 * - Multiple threads can subscribe concurrently
 * - Handlers are called synchronously in emitting thread
 * - Per-event-type locking for better concurrency
 */
class EventBus {
public:
    EventBus() = default;
    ~EventBus() = default;

    // Non-copyable (would duplicate handlers)
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    // Moveable
    EventBus(EventBus&&) = default;
    EventBus& operator=(EventBus&&) = default;

    /**
     * @brief Subscribe to events of a specific type
     *
     * TEMPLATE PARAMETERS:
     * EventType - The event type to subscribe to (e.g., FileAddedEvent)
     *
     * PARAMETERS:
     * handler - Function called when event is emitted
     *           Signature: void(const EventType&)
     *
     * RETURNS:
     * Subscription ID for unsubscribing later
     *
     * EXAMPLE:
     * auto id = bus.subscribe<FileAddedEvent>([](const FileAddedEvent& e) {
     *     spdlog::info("File added: {}", e.metadata.file_path);
     * });
     */
    template<typename EventType>
    size_t subscribe(std::function<void(const EventType&)> handler) {
        std::unique_lock lock(mutex_);

        // Get type index for this event type
        auto type_id = std::type_index(typeid(EventType));

        // Create handler wrapper
        auto wrapper = std::make_unique<HandlerImpl<EventType>>(std::move(handler));
        size_t handler_id = next_handler_id_++;

        // Store handler
        handlers_[type_id].push_back({handler_id, std::move(wrapper)});

        return handler_id;
    }

    /**
     * @brief Unsubscribe a specific handler
     *
     * PARAMETERS:
     * handler_id - ID returned from subscribe()
     *
     * EXAMPLE:
     * size_t id = bus.subscribe<MyEvent>(...);
     * // Later...
     * bus.unsubscribe(id);
     */
    template<typename EventType>
    void unsubscribe(size_t handler_id) {
        std::unique_lock lock(mutex_);

        auto type_id = std::type_index(typeid(EventType));
        auto it = handlers_.find(type_id);

        if (it != handlers_.end()) {
            auto& handler_list = it->second;
            handler_list.erase(
                std::remove_if(handler_list.begin(), handler_list.end(),
                    [handler_id](const auto& pair) {
                        return pair.first == handler_id;
                    }),
                handler_list.end()
            );
        }
    }

    /**
     * @brief Emit an event to all subscribers
     *
     * TEMPLATE PARAMETERS:
     * EventType - The event type (usually deduced from argument)
     *
     * PARAMETERS:
     * event - The event to emit
     *
     * HOW IT WORKS:
     * 1. Lock handlers for this event type
     * 2. Call each handler synchronously
     * 3. Handlers run in emitting thread
     *
     * EXCEPTION SAFETY:
     * If a handler throws, exception is caught and logged,
     * remaining handlers still execute.
     *
     * EXAMPLE:
     * bus.emit(FileAddedEvent{metadata, "http"});
     */
    template<typename EventType>
    void emit(const EventType& event) {
        // Get handlers (make a copy to avoid deadlock if handler subscribes)
        std::vector<std::shared_ptr<HandlerBase>> handlers_copy;
        {
            std::shared_lock lock(mutex_);
            auto type_id = std::type_index(typeid(EventType));
            auto it = handlers_.find(type_id);

            if (it == handlers_.end()) {
                return;  // No subscribers for this event
            }

            // Copy handler pointers
            for (const auto& [id, handler] : it->second) {
                handlers_copy.push_back(handler);
            }
        }

        // Call handlers without holding lock
        for (auto& handler : handlers_copy) {
            try {
                handler->call(&event);
            } catch (const std::exception& e) {
                // Log but don't crash - one bad handler shouldn't kill all
                // (In production, use spdlog)
                // spdlog::error("Event handler exception: {}", e.what());
            }
        }
    }

    /**
     * @brief Get number of subscribers for an event type
     */
    template<typename EventType>
    size_t subscriber_count() const {
        std::shared_lock lock(mutex_);
        auto type_id = std::type_index(typeid(EventType));
        auto it = handlers_.find(type_id);
        return it != handlers_.end() ? it->second.size() : 0;
    }

    /**
     * @brief Remove all subscribers
     */
    void clear() {
        std::unique_lock lock(mutex_);
        handlers_.clear();
    }

private:
    // ════════════════════════════════════════════════════════
    // Type Erasure Implementation
    // ════════════════════════════════════════════════════════

    /**
     * @brief Base class for type-erased handlers
     */
    struct HandlerBase {
        virtual ~HandlerBase() = default;
        virtual void call(const void* event) = 0;
    };

    /**
     * @brief Typed handler implementation
     */
    template<typename EventType>
    struct HandlerImpl : HandlerBase {
        std::function<void(const EventType&)> func;

        explicit HandlerImpl(std::function<void(const EventType&)> f)
            : func(std::move(f)) {}

        void call(const void* event) override {
            // Safe cast - we only store EventType* for EventType handlers
            const EventType* typed_event = static_cast<const EventType*>(event);
            func(*typed_event);
        }
    };

    // ════════════════════════════════════════════════════════
    // Member Variables
    // ════════════════════════════════════════════════════════

    // Map: event type -> list of (handler_id, handler)
    std::unordered_map<
        std::type_index,
        std::vector<std::pair<size_t, std::shared_ptr<HandlerBase>>>
    > handlers_;

    // Thread safety
    mutable std::shared_mutex mutex_;

    // Handler ID counter
    size_t next_handler_id_ = 0;
};

} // namespace dfs::events
```

### Testing the Event Bus

#### Test File: `tests/events/event_bus_test.cpp`

```cpp
#include <gtest/gtest.h>
#include "dfs/events/event_bus.hpp"
#include <atomic>
#include <thread>

using namespace dfs::events;

// Test event types
struct TestEvent {
    int value;
    std::string message;
};

struct AnotherEvent {
    double data;
};

TEST(EventBus, SubscribeAndEmit) {
    EventBus bus;

    bool handler_called = false;
    int received_value = 0;

    bus.subscribe<TestEvent>([&](const TestEvent& e) {
        handler_called = true;
        received_value = e.value;
    });

    bus.emit(TestEvent{42, "test"});

    EXPECT_TRUE(handler_called);
    EXPECT_EQ(received_value, 42);
}

TEST(EventBus, MultipleSubscribers) {
    EventBus bus;

    int count = 0;

    bus.subscribe<TestEvent>([&](const TestEvent& e) { count++; });
    bus.subscribe<TestEvent>([&](const TestEvent& e) { count++; });
    bus.subscribe<TestEvent>([&](const TestEvent& e) { count++; });

    bus.emit(TestEvent{1, "test"});

    EXPECT_EQ(count, 3);
}

TEST(EventBus, DifferentEventTypes) {
    EventBus bus;

    int test_count = 0;
    int another_count = 0;

    bus.subscribe<TestEvent>([&](const TestEvent& e) { test_count++; });
    bus.subscribe<AnotherEvent>([&](const AnotherEvent& e) { another_count++; });

    bus.emit(TestEvent{1, "test"});
    bus.emit(AnotherEvent{3.14});
    bus.emit(TestEvent{2, "test2"});

    EXPECT_EQ(test_count, 2);
    EXPECT_EQ(another_count, 1);
}

TEST(EventBus, Unsubscribe) {
    EventBus bus;

    int count = 0;
    auto id = bus.subscribe<TestEvent>([&](const TestEvent& e) { count++; });

    bus.emit(TestEvent{1, "test"});
    EXPECT_EQ(count, 1);

    bus.unsubscribe<TestEvent>(id);

    bus.emit(TestEvent{2, "test"});
    EXPECT_EQ(count, 1);  // Still 1, handler was removed
}

TEST(EventBus, NoSubscribers) {
    EventBus bus;

    // Should not crash when no subscribers
    EXPECT_NO_THROW(bus.emit(TestEvent{1, "test"}));
}

TEST(EventBus, ThreadSafety) {
    EventBus bus;
    std::atomic<int> count{0};

    // Subscribe from multiple threads
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&bus, &count]() {
            bus.subscribe<TestEvent>([&count](const TestEvent& e) {
                count++;
            });
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Emit event
    bus.emit(TestEvent{42, "test"});

    EXPECT_EQ(count, 10);
}

TEST(EventBus, ConcurrentEmit) {
    EventBus bus;
    std::atomic<int> count{0};

    bus.subscribe<TestEvent>([&count](const TestEvent& e) {
        count += e.value;
    });

    // Emit from multiple threads
    std::vector<std::thread> threads;
    for (int i = 0; i < 100; ++i) {
        threads.emplace_back([&bus, i]() {
            bus.emit(TestEvent{1, "test"});
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(count, 100);
}

TEST(EventBus, SubscriberCount) {
    EventBus bus;

    EXPECT_EQ(bus.subscriber_count<TestEvent>(), 0);

    auto id1 = bus.subscribe<TestEvent>([](const TestEvent& e) {});
    EXPECT_EQ(bus.subscriber_count<TestEvent>(), 1);

    auto id2 = bus.subscribe<TestEvent>([](const TestEvent& e) {});
    EXPECT_EQ(bus.subscriber_count<TestEvent>(), 2);

    bus.unsubscribe<TestEvent>(id1);
    EXPECT_EQ(bus.subscriber_count<TestEvent>(), 1);
}
```

---

## Task 3: Thread-Safe Event Queue

### Goal
Implement an async event queue for background event processing.

### Why Event Queue?

**Synchronous emit (current):**
```cpp
bus.emit(event);  // Blocks until all handlers complete
// If handlers are slow, this takes forever!
```

**Asynchronous with queue:**
```cpp
bus.emit_async(event);  // Returns immediately
// Worker thread processes events in background
```

### Implementation

#### Header: `include/dfs/events/event_queue.hpp`

```cpp
/**
 * @file event_queue.hpp
 * @brief Thread-safe event queue for async event processing
 */

#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <chrono>

namespace dfs::events {

/**
 * @brief Thread-safe FIFO queue
 *
 * THREAD SAFETY:
 * - Multiple producers can push concurrently
 * - Multiple consumers can pop concurrently
 * - Uses condition variable for blocking wait
 */
template<typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() = default;

    /**
     * @brief Push item to queue
     *
     * THREAD SAFE: Yes
     * BLOCKS: No
     */
    void push(T item) {
        {
            std::unique_lock lock(mutex_);
            queue_.push(std::move(item));
        }
        cv_.notify_one();  // Wake up one waiting consumer
    }

    /**
     * @brief Try to pop item (non-blocking)
     *
     * RETURNS: Item if available, nullopt if queue empty
     * THREAD SAFE: Yes
     * BLOCKS: No
     */
    std::optional<T> try_pop() {
        std::unique_lock lock(mutex_);

        if (queue_.empty()) {
            return std::nullopt;
        }

        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    /**
     * @brief Pop item (blocking)
     *
     * RETURNS: Item (waits until available)
     * THREAD SAFE: Yes
     * BLOCKS: Yes, until item available or shutdown
     */
    std::optional<T> pop() {
        std::unique_lock lock(mutex_);

        // Wait until item available or shutting down
        cv_.wait(lock, [this]() {
            return !queue_.empty() || shutdown_;
        });

        if (shutdown_ && queue_.empty()) {
            return std::nullopt;
        }

        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    /**
     * @brief Pop item with timeout
     *
     * RETURNS: Item if available within timeout
     * THREAD SAFE: Yes
     * BLOCKS: Yes, up to timeout duration
     */
    template<typename Rep, typename Period>
    std::optional<T> pop_for(const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock lock(mutex_);

        if (!cv_.wait_for(lock, timeout, [this]() {
            return !queue_.empty() || shutdown_;
        })) {
            return std::nullopt;  // Timeout
        }

        if (shutdown_ && queue_.empty()) {
            return std::nullopt;
        }

        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    /**
     * @brief Get queue size
     */
    size_t size() const {
        std::unique_lock lock(mutex_);
        return queue_.size();
    }

    /**
     * @brief Check if queue is empty
     */
    bool empty() const {
        std::unique_lock lock(mutex_);
        return queue_.empty();
    }

    /**
     * @brief Signal shutdown (wake up all waiting threads)
     */
    void shutdown() {
        {
            std::unique_lock lock(mutex_);
            shutdown_ = true;
        }
        cv_.notify_all();
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool shutdown_ = false;
};

} // namespace dfs::events
```

### Async Event Bus

Add to `EventBus`:

```cpp
class EventBus {
public:
    /**
     * @brief Emit event asynchronously
     *
     * Event is queued and processed by background thread.
     * Returns immediately.
     */
    template<typename EventType>
    void emit_async(EventType event) {
        event_queue_.push([this, event = std::move(event)]() {
            this->emit(event);
        });
    }

    /**
     * @brief Start background event processing thread
     */
    void start_async_processing() {
        worker_thread_ = std::thread([this]() {
            while (true) {
                auto task = event_queue_.pop();
                if (!task.has_value()) {
                    break;  // Shutdown
                }
                task.value()();  // Execute event emission
            }
        });
    }

    /**
     * @brief Stop background processing
     */
    void stop_async_processing() {
        event_queue_.shutdown();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

private:
    ThreadSafeQueue<std::function<void()>> event_queue_;
    std::thread worker_thread_;
};
```

---

## Task 4: Component System

### Goal
Create a component-based architecture where components subscribe to events.

### Component Base Class

```cpp
/**
 * @file component.hpp
 * @brief Base class for event-driven components
 */

#pragma once

#include "dfs/events/event_bus.hpp"
#include <vector>

namespace dfs::events {

/**
 * @brief Base class for components that react to events
 *
 * USAGE:
 * 1. Inherit from Component
 * 2. Override on_xxx() methods for events you care about
 * 3. Call register_handlers() in constructor
 *
 * EXAMPLE:
 * class Logger : public Component {
 * public:
 *     Logger(EventBus& bus) : Component(bus) {
 *         subscribe<FileAddedEvent>(&Logger::on_file_added);
 *     }
 * private:
 *     void on_file_added(const FileAddedEvent& e) {
 *         spdlog::info("File added: {}", e.metadata.file_path);
 *     }
 * };
 */
class Component {
public:
    explicit Component(EventBus& bus) : bus_(bus) {}

    virtual ~Component() {
        // Auto-unsubscribe on destruction
        for (auto [type_id, handler_id] : subscriptions_) {
            // TODO: Unsubscribe by type_id and handler_id
        }
    }

protected:
    /**
     * @brief Subscribe to an event type
     *
     * TEMPLATE PARAMETERS:
     * EventType - Event to subscribe to
     * MemberFunc - Member function pointer (e.g., &MyClass::on_event)
     */
    template<typename EventType, typename Derived>
    void subscribe(void (Derived::*handler)(const EventType&)) {
        auto handler_id = bus_.subscribe<EventType>(
            [this, handler](const EventType& e) {
                // Call member function
                (static_cast<Derived*>(this)->*handler)(e);
            }
        );

        subscriptions_.push_back({std::type_index(typeid(EventType)), handler_id});
    }

    EventBus& bus_;

private:
    std::vector<std::pair<std::type_index, size_t>> subscriptions_;
};

} // namespace dfs::events
```

### Example Components

```cpp
/**
 * @brief Logger component - logs all file events
 */
class LoggerComponent : public Component {
public:
    explicit LoggerComponent(EventBus& bus) : Component(bus) {
        subscribe<FileAddedEvent>(&LoggerComponent::on_file_added);
        subscribe<FileModifiedEvent>(&LoggerComponent::on_file_modified);
        subscribe<FileDeletedEvent>(&LoggerComponent::on_file_deleted);
    }

private:
    void on_file_added(const FileAddedEvent& e) {
        spdlog::info("[FileAdded] path={} hash={} size={} source={}",
            e.metadata.file_path,
            e.metadata.hash,
            e.metadata.size,
            e.source);
    }

    void on_file_modified(const FileModifiedEvent& e) {
        spdlog::info("[FileModified] path={} old_hash={} new_hash={}",
            e.file_path,
            e.old_hash,
            e.new_hash);
    }

    void on_file_deleted(const FileDeletedEvent& e) {
        spdlog::info("[FileDeleted] path={}", e.file_path);
    }
};

/**
 * @brief Sync component - queues files for synchronization
 */
class SyncComponent : public Component {
public:
    explicit SyncComponent(EventBus& bus) : Component(bus) {
        subscribe<FileAddedEvent>(&SyncComponent::on_file_added);
        subscribe<FileModifiedEvent>(&SyncComponent::on_file_modified);
    }

    size_t queue_size() const {
        std::lock_guard lock(mutex_);
        return sync_queue_.size();
    }

private:
    void on_file_added(const FileAddedEvent& e) {
        std::lock_guard lock(mutex_);
        sync_queue_.push(e.metadata.file_path);
        spdlog::debug("Queued for sync: {}", e.metadata.file_path);
    }

    void on_file_modified(const FileModifiedEvent& e) {
        std::lock_guard lock(mutex_);
        sync_queue_.push(e.file_path);
        spdlog::debug("Queued for sync: {}", e.file_path);
    }

    mutable std::mutex mutex_;
    std::queue<std::string> sync_queue_;
};

/**
 * @brief Metrics component - tracks statistics
 */
class MetricsComponent : public Component {
public:
    explicit MetricsComponent(EventBus& bus) : Component(bus) {
        subscribe<FileAddedEvent>(&MetricsComponent::on_file_added);
        subscribe<FileModifiedEvent>(&MetricsComponent::on_file_modified);
        subscribe<FileDeletedEvent>(&MetricsComponent::on_file_deleted);
    }

    struct Stats {
        std::atomic<uint64_t> files_added{0};
        std::atomic<uint64_t> files_modified{0};
        std::atomic<uint64_t> files_deleted{0};
        std::atomic<uint64_t> total_bytes_added{0};
    };

    const Stats& get_stats() const { return stats_; }

private:
    void on_file_added(const FileAddedEvent& e) {
        stats_.files_added++;
        stats_.total_bytes_added += e.metadata.size;
    }

    void on_file_modified(const FileModifiedEvent& e) {
        stats_.files_modified++;
    }

    void on_file_deleted(const FileDeletedEvent& e) {
        stats_.files_deleted++;
    }

    Stats stats_;
};
```

---

## Task 5: Event Filtering & Priorities

### Event Filtering

```cpp
/**
 * @brief Subscribe with filter predicate
 */
template<typename EventType, typename Predicate>
size_t subscribe_filtered(
    std::function<void(const EventType&)> handler,
    Predicate filter)
{
    return subscribe<EventType>([handler, filter](const EventType& e) {
        if (filter(e)) {
            handler(e);
        }
    });
}

// Usage
bus.subscribe_filtered<FileAddedEvent>(
    [](const FileAddedEvent& e) {
        spdlog::info("Large file added: {}", e.metadata.file_path);
    },
    [](const FileAddedEvent& e) {
        return e.metadata.size > 1024 * 1024;  // Only files > 1MB
    }
);
```

### Priority Events

```cpp
struct PriorityEvent {
    int priority;  // Higher = more important

    bool operator<(const PriorityEvent& other) const {
        return priority < other.priority;  // Min-heap -> max priority
    }
};

template<typename T>
class PriorityEventQueue {
    std::priority_queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;

public:
    void push(T event) {
        {
            std::lock_guard lock(mutex_);
            queue_.push(std::move(event));
        }
        cv_.notify_one();
    }

    std::optional<T> pop() {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this]() { return !queue_.empty(); });

        T event = std::move(queue_.top());
        queue_.pop();
        return event;
    }
};
```

---

## Task 6: Integration with Metadata System

### Update HTTP Handlers

```cpp
/**
 * @file metadata_server_example.cpp (updated)
 */

#include "dfs/events/event_bus.hpp"
#include "dfs/metadata/types.hpp"

using namespace dfs::events;
using namespace dfs::metadata;

// Global event bus
EventBus g_event_bus;

// Event types for metadata operations
struct FileAddedEvent {
    FileMetadata metadata;
    std::string source;  // "http", "watcher", "sync"
};

struct FileModifiedEvent {
    std::string file_path;
    std::string old_hash;
    std::string new_hash;
    size_t new_size;
};

struct FileDeletedEvent {
    std::string file_path;
    FileMetadata last_metadata;
};

// Updated handler
HttpResponse handle_add_metadata(const HttpContext& ctx) {
    std::string ddl = ctx.request.body_as_string();

    Parser parser(ddl);
    auto parse_result = parser.parse_file_metadata();

    if (parse_result.is_error()) {
        // ... error handling ...
    }

    FileMetadata metadata = parse_result.value();

    // Add to store
    auto add_result = g_metadata_store.add(metadata);

    if (add_result.is_error()) {
        // ... error handling ...
    }

    // NEW: Emit event (components will react automatically!)
    g_event_bus.emit(FileAddedEvent{metadata, "http"});

    // Return response
    json success_json = {
        {"status", "added"},
        {"file_path", metadata.file_path},
        {"hash", metadata.hash},
        {"size", metadata.size}
    };

    HttpResponse response(HttpStatus::CREATED);
    response.set_body(success_json.dump(2));
    response.set_header("Content-Type", "application/json");
    return response;
}

int main() {
    // Setup components
    LoggerComponent logger(g_event_bus);
    SyncComponent sync_manager(g_event_bus);
    MetricsComponent metrics(g_event_bus);

    // Setup HTTP server
    // ... rest of server code ...

    // At shutdown, print metrics
    auto stats = metrics.get_stats();
    spdlog::info("Session stats:");
    spdlog::info("  Files added: {}", stats.files_added.load());
    spdlog::info("  Files modified: {}", stats.files_modified.load());
    spdlog::info("  Files deleted: {}", stats.files_deleted.load());
    spdlog::info("  Total bytes: {}", stats.total_bytes_added.load());

    return 0;
}
```

---

## Task 7: Testing & Benchmarking

### Performance Benchmark

```cpp
TEST(EventBus, PerformanceBenchmark) {
    EventBus bus;
    std::atomic<uint64_t> count{0};

    // Subscribe 10 handlers
    for (int i = 0; i < 10; ++i) {
        bus.subscribe<TestEvent>([&count](const TestEvent& e) {
            count++;
        });
    }

    // Benchmark
    const size_t NUM_EVENTS = 1'000'000;

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < NUM_EVENTS; ++i) {
        bus.emit(TestEvent{static_cast<int>(i), "test"});
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    double events_per_sec = (NUM_EVENTS * 1000.0) / duration.count();
    double handlers_per_sec = (count.load() * 1000.0) / duration.count();

    std::cout << "Emitted " << NUM_EVENTS << " events in " << duration.count() << "ms\n";
    std::cout << "Events/sec: " << events_per_sec << "\n";
    std::cout << "Handler calls/sec: " << handlers_per_sec << "\n";
    std::cout << "Total handler calls: " << count.load() << "\n";

    EXPECT_EQ(count.load(), NUM_EVENTS * 10);
    EXPECT_GT(events_per_sec, 100'000);  // Should handle 100k+ events/sec
}
```

### Integration Test

```cpp
TEST(Integration, EventDrivenMetadataSystem) {
    EventBus bus;
    MetadataStore store;

    // Setup components
    std::vector<std::string> logged_events;
    bus.subscribe<FileAddedEvent>([&](const FileAddedEvent& e) {
        logged_events.push_back("added:" + e.metadata.file_path);
    });

    std::queue<std::string> sync_queue;
    bus.subscribe<FileAddedEvent>([&](const FileAddedEvent& e) {
        sync_queue.push(e.metadata.file_path);
    });

    // Simulate HTTP request
    FileMetadata metadata;
    metadata.file_path = "/test.txt";
    metadata.hash = "abc123";
    metadata.size = 1024;

    store.add(metadata);
    bus.emit(FileAddedEvent{metadata, "http"});

    // Verify both components reacted
    EXPECT_EQ(logged_events.size(), 1);
    EXPECT_EQ(logged_events[0], "added:/test.txt");

    EXPECT_EQ(sync_queue.size(), 1);
    EXPECT_EQ(sync_queue.front(), "/test.txt");
}
```

---

## Learning Objectives

After completing Phase 3, you will understand:

### 1. **Design Patterns**
- **Observer Pattern** - Object notifies dependents of state changes
- **Publisher-Subscriber** - Decoupled event-driven communication
- **Component Pattern** - Modular, composable systems

### 2. **Advanced C++ Techniques**

#### Template Metaprogramming
```cpp
template<typename EventType>
void subscribe(std::function<void(const EventType&)> handler);
// Different function for each EventType, generated at compile-time
```

#### Type Erasure
```cpp
// Store different types uniformly
std::unordered_map<std::type_index, std::vector<std::unique_ptr<HandlerBase>>>
```

#### `std::type_index`
```cpp
auto type_id = std::type_index(typeid(EventType));
// Get unique ID for each type at runtime
```

#### Perfect Forwarding (Advanced)
```cpp
template<typename EventType>
void emit(EventType&& event) {
    // Preserve value category (lvalue vs rvalue)
}
```

### 3. **Concurrency Patterns**

#### Reader-Writer Locks
```cpp
std::shared_mutex mutex_;
std::shared_lock lock(mutex_);  // Multiple readers
std::unique_lock lock(mutex_);  // Exclusive writer
```

#### Producer-Consumer Queue
```cpp
ThreadSafeQueue<Event> queue_;
producer: queue_.push(event);
consumer: auto event = queue_.pop();
```

#### Condition Variables
```cpp
std::condition_variable cv_;
cv_.wait(lock, []() { return !queue_.empty(); });
cv_.notify_one();
```

### 4. **Architectural Principles**

- **Loose Coupling** - Components don't depend on each other
- **High Cohesion** - Each component has one responsibility
- **Open/Closed Principle** - Open for extension, closed for modification
- **Dependency Inversion** - Depend on abstractions (events), not concrete classes

---

## Advanced C++ Patterns Used

### 1. Template Metaprogramming

**What:** Code that runs at compile-time to generate code.

```cpp
template<typename EventType>
void emit(const EventType& event) {
    // Compiler generates separate function for each EventType
}

// Generates:
// void emit<FileAddedEvent>(const FileAddedEvent&)
// void emit<FileDeletedEvent>(const FileDeletedEvent&)
// etc.
```

### 2. Type Erasure

**What:** Store different types in same container by hiding type behind interface.

```cpp
// Without type erasure - impossible!
std::vector<std::function<void(const ???&)>> handlers;

// With type erasure - works!
struct HandlerBase { virtual void call(const void*) = 0; };
template<typename T> struct HandlerImpl : HandlerBase { ... };
std::vector<std::unique_ptr<HandlerBase>> handlers;
```

### 3. RAII (Resource Acquisition Is Initialization)

```cpp
class Component {
    ~Component() {
        // Auto-unsubscribe when component destroyed
        for (auto id : subscription_ids_) {
            bus_.unsubscribe(id);
        }
    }
};
```

### 4. Move Semantics

```cpp
void push(T item) {
    queue_.push(std::move(item));  // Transfer ownership, avoid copy
}
```

---

## Real-World Applications

### Where This Pattern Is Used

#### 1. **Game Engines**
```cpp
// Unity-style ECS
struct CollisionEvent { Entity a, b; };

PhysicsSystem.emit(CollisionEvent{player, enemy});

// Multiple systems react:
AudioSystem: Play collision sound
ParticleSystem: Spawn explosion effect
ScoreSystem: Add points
```

#### 2. **GUI Frameworks**
```cpp
// Qt Signals/Slots
button.clicked.connect([]() { ... });
slider.valueChanged.connect([](int val) { ... });

// Windows Messages
WM_PAINT, WM_MOUSEMOVE, WM_KEYDOWN
```

#### 3. **Web Development**
```javascript
// React-style events
<button onClick={handleClick}>
addEventListener('load', () => { ... });
```

#### 4. **Distributed Systems**
```cpp
// Kafka, RabbitMQ, Redis Pub/Sub
topic.publish(OrderCreatedEvent{...});

// Microservices react:
InventoryService: Reserve items
PaymentService: Charge card
EmailService: Send confirmation
```

---

## How This Fits Into The System

### Before Phase 3 (Tightly Coupled)

```
┌──────────────────────────────────────┐
│         HTTP Handler                 │
│  - Parse request                     │
│  - Update store         ┌────────┐   │
│  - Log ───────────────> │ Logger │   │
│  - Sync ──────────────> │ Sync   │   │
│  - Metrics ───────────> │Metrics │   │
│  - Return response      └────────┘   │
└──────────────────────────────────────┘
         (Handler knows about everyone)
```

### After Phase 3 (Event-Driven)

```
┌──────────────────────────────────────┐
│         HTTP Handler                 │
│  - Parse request                     │
│  - Update store                      │
│  - Emit event ───────────┐           │
│  - Return response       │           │
└──────────────────────────┼───────────┘
                           │
                           ▼
                   ┌──────────────┐
                   │  Event Bus   │
                   └──────┬───────┘
                          │
         ┌────────────────┼────────────────┐
         │                │                │
         ▼                ▼                ▼
    ┌────────┐      ┌────────┐      ┌─────────┐
    │ Logger │      │  Sync  │      │ Metrics │
    └────────┘      └────────┘      └─────────┘
    (All subscribe independently!)
```

---

## Next Steps (Phase 4)

Phase 4 will add **File Transfer** capabilities, using events to coordinate:

```cpp
// Phase 4: File transfer events
struct FileUploadStartedEvent {
    std::string file_path;
    size_t total_bytes;
};

struct FileChunkReceivedEvent {
    std::string file_path;
    size_t chunk_index;
    size_t bytes_received;
};

struct FileUploadCompletedEvent {
    std::string file_path;
    std::string hash;
    size_t total_bytes;
    std::chrono::milliseconds duration;
};

// Components react:
ProgressTracker: Update UI with percent complete
MetadataStore: Update file hash after upload
SyncManager: Mark file as synced
Logger: Log upload completion
```

---

## Summary

### What Phase 3 Delivers

✅ **Decoupled Architecture** - Components don't know about each other
✅ **Extensible** - Add new features without modifying existing code
✅ **Testable** - Mock events, test in isolation
✅ **Type-Safe** - Compiler catches event type errors
✅ **Thread-Safe** - Concurrent event emission and subscription
✅ **Modern C++** - Advanced template and concurrency patterns

### Files to Create

```
include/dfs/events/
  ├── event_bus.hpp           (Type-safe event bus)
  ├── event_queue.hpp         (Thread-safe queue)
  ├── component.hpp           (Component base class)
  └── events.hpp              (Event type definitions)

src/events/
  └── CMakeLists.txt          (Build configuration)

examples/
  └── metadata_server_example.cpp  (Updated with events)

tests/events/
  ├── event_bus_test.cpp      (Unit tests)
  ├── event_queue_test.cpp    (Queue tests)
  └── integration_test.cpp    (Full system test)

docs/
  └── phase_3_reference.md    (This file!)
```

### Development Checklist

- [ ] Task 1: Design event system architecture
- [ ] Task 2: Implement type-safe EventBus
- [ ] Task 3: Implement thread-safe event queue
- [ ] Task 4: Create component system
- [ ] Task 5: Add event filtering & priorities
- [ ] Task 6: Integrate with metadata system
- [ ] Task 7: Testing & benchmarks
- [ ] Documentation complete
- [ ] Performance meets goals (100k+ events/sec)
- [ ] Ready for Phase 4!

---

**Phase 3 transforms our system from a monolithic application into a flexible, event-driven architecture. This pattern is used in game engines, GUI frameworks, and distributed systems - skills that transfer to any modern C++ project!** 🚀
