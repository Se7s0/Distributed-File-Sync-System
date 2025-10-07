#include <gtest/gtest.h>
#include "dfs/events/event_queue.hpp"
#include <thread>
#include <chrono>

using namespace dfs::events;

TEST(ThreadSafeQueue, PushAndPop) {
    ThreadSafeQueue<int> queue;

    queue.push(42);
    queue.push(100);

    auto val1 = queue.pop();
    ASSERT_TRUE(val1.has_value());
    EXPECT_EQ(val1.value(), 42);

    auto val2 = queue.pop();
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(val2.value(), 100);
}

TEST(ThreadSafeQueue, TryPop) {
    ThreadSafeQueue<int> queue;

    auto val = queue.try_pop();
    EXPECT_FALSE(val.has_value());  // Empty queue

    queue.push(123);

    val = queue.try_pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 123);
}

TEST(ThreadSafeQueue, PopTimeout) {
    ThreadSafeQueue<int> queue;

    auto start = std::chrono::high_resolution_clock::now();
    auto val = queue.pop_for(std::chrono::milliseconds(100));
    auto end = std::chrono::high_resolution_clock::now();

    EXPECT_FALSE(val.has_value());  // Timeout

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_GE(duration.count(), 90);  // At least 90ms (some tolerance)
}

TEST(ThreadSafeQueue, Size) {
    ThreadSafeQueue<int> queue;

    EXPECT_EQ(queue.size(), 0);
    EXPECT_TRUE(queue.empty());

    queue.push(1);
    EXPECT_EQ(queue.size(), 1);
    EXPECT_FALSE(queue.empty());

    queue.push(2);
    EXPECT_EQ(queue.size(), 2);

    queue.pop();
    EXPECT_EQ(queue.size(), 1);
}

TEST(ThreadSafeQueue, Shutdown) {
    ThreadSafeQueue<int> queue;

    queue.shutdown();

    auto val = queue.pop();
    EXPECT_FALSE(val.has_value());  // Shutdown returns nullopt
}

TEST(ThreadSafeQueue, ProducerConsumer) {
    ThreadSafeQueue<int> queue;
    std::atomic<int> sum{0};

    // Producer thread
    std::thread producer([&queue]() {
        for (int i = 0; i < 100; ++i) {
            queue.push(i);
        }
        queue.shutdown();  // Signal done
    });

    // Consumer thread
    std::thread consumer([&queue, &sum]() {
        while (true) {
            auto val = queue.pop();
            if (!val.has_value()) {
                break;  // Shutdown
            }
            sum += val.value();
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(sum, 4950);  // Sum of 0..99
}
