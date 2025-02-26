#include <boost/lockfree/queue.hpp>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <iostream>
#include <thread>

// Bounded channel with overflow policy
// - Block: block the sender until there is space in the queue
// - DropOldest: drop the oldest message in the queue
// - Reject: reject the message
enum class OverflowPolicy {
    Block,
    DropOldest,
    Reject
};

template <typename T>
class BoundedChannel {
private:
    boost::lockfree::queue<T> queue;
    std::mutex mtx;
    std::condition_variable cv;
    bool closed = false;
    size_t capacity;
    OverflowPolicy policy;

public:
	// Initialize the channel with a given capacity and overflow policy
	// - capacity: the maximum number of elements in the queue
	// - policy: the overflow policy
    explicit BoundedChannel(size_t capacity, OverflowPolicy policy = OverflowPolicy::Block)
        : queue(capacity), capacity(capacity), policy(policy) {
    }

	// Send a value to the channel
    bool Send(T value) {
        std::unique_lock<std::mutex> lock(mtx);

        if (closed) return false;

        if (queue.push(std::move(value))) {
            cv.notify_one();
            return true;
        }

        switch (policy) {
        case OverflowPolicy::Reject:
            return false;

        case OverflowPolicy::DropOldest: {
            T temp;
            if (queue.pop(temp)) {
                queue.push(std::move(value));
                cv.notify_one();
                return true;
            }
            return false;
        }

        case OverflowPolicy::Block:
        default: {
            while (!closed) {
                bool canPush = queue.push(T());
                if (canPush) {
                    T dummy;
                    queue.pop(dummy);

                    queue.push(std::move(value));
                    cv.notify_one();
                    return true;
                }
                cv.wait(lock);
            }
            return false;
        }
        }

        return false;
    }

	// Receive a value from the channel
    std::optional<T> Receive() {
        std::unique_lock<std::mutex> lock(mtx);

        cv.wait(lock, [this] { return !queue.empty() || closed; });

        if (queue.empty()) {
            return std::nullopt;
        }

        T value;
        queue.pop(value);

        cv.notify_one();

        return value;
    }

	// Try to receive a value without blocking
    std::optional<T> TryReceive() {
        T value;
        if (queue.pop(value)) {
            {
                std::unique_lock<std::mutex> lock(mtx);
                cv.notify_one();
            }
            return value;
        }
        return std::nullopt;
    }

	// Close the channel
    void Close() {
        std::unique_lock<std::mutex> lock(mtx);
        closed = true;
        cv.notify_all();
    }
};
