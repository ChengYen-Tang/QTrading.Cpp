#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <iostream>
#include <thread>

template <typename T>
class UnboundedChannel {
private:
    std::queue<T> queue;
    std::mutex mtx;
    std::condition_variable cv;
    bool closed = false;

public:
	// Send a value to the channel
    bool Send(T value) {
        {
            std::unique_lock<std::mutex> lock(mtx);
            if (closed) return false;
            queue.push(std::move(value));
        }

        cv.notify_one();
        return true;
    }

	// Receive a value from the channel
    std::optional<T> Receive() {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this] { return !queue.empty() || closed; });
        if (queue.empty()) return std::nullopt;

        T value = std::move(queue.front());
        queue.pop();
        return value;
    }

	// Try to receive a value without blocking
    std::optional<T> TryReceive() {
        std::unique_lock<std::mutex> lock(mtx);
        if (queue.empty()) return std::nullopt;

        T value = std::move(queue.front());
        queue.pop();
        return value;
    }

	// Close the channel
    void Close() {
        std::unique_lock<std::mutex> lock(mtx);
        closed = true;
        cv.notify_all();
    }
};
