/**
 * @file event_queue.hpp
 * @brief Thread-safe event queue for async event processing
 *
 * WHY THIS FILE EXISTS:
 * Provides async event processing via a background thread.
 * Allows emitting events without blocking on handler execution.
 *
 * EXAMPLE:
 * ThreadSafeQueue<Event> queue;
 * queue.push(event);  // Producer
 * auto event = queue.pop();  // Consumer (blocks until available)
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

    // Non-copyable
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

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

    /**
     * @brief Reset shutdown flag
     */
    void reset() {
        std::unique_lock lock(mutex_);
        shutdown_ = false;
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool shutdown_ = false;
};

} // namespace dfs::events
