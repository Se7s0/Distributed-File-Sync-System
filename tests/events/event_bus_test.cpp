#include <gtest/gtest.h>
#include "dfs/events/event_bus.hpp"
#include <atomic>
#include <thread>
#include <vector>

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

TEST(EventBus, Clear) {
    EventBus bus;

    bus.subscribe<TestEvent>([](const TestEvent& e) {});
    bus.subscribe<AnotherEvent>([](const AnotherEvent& e) {});

    EXPECT_EQ(bus.subscriber_count<TestEvent>(), 1);
    EXPECT_EQ(bus.subscriber_count<AnotherEvent>(), 1);

    bus.clear();

    EXPECT_EQ(bus.subscriber_count<TestEvent>(), 0);
    EXPECT_EQ(bus.subscriber_count<AnotherEvent>(), 0);
}
