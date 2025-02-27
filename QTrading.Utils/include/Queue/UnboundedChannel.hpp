#include <queue>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <thread>
#include "Channel.hpp"

namespace QTrading::Utils::Queue
{
    template <typename T>
    class UnboundedChannel : public Channel<T> {
    private:
        std::queue<T> queue;
        std::mutex mtx;
        std::condition_variable cv;

    public:
        // Send a value to the channel
        bool Send(T value) override {
            {
                std::unique_lock<std::mutex> lock(mtx);
                if (this->closed_) return false;
                queue.push(std::move(value));
            }

            cv.notify_one();
            return true;
        }

        // Receive a value from the channel
        std::optional<T> Receive() override {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [this] { return !queue.empty() || this->closed_; });
            if (queue.empty()) return std::nullopt;

            T value = std::move(queue.front());
            queue.pop();
            return value;
        }

        // Try to receive a value without blocking
        std::optional<T> TryReceive() override {
            std::unique_lock<std::mutex> lock(mtx);
            if (queue.empty()) return std::nullopt;

            T value = std::move(queue.front());
            queue.pop();
            return value;
        }

        // Close the channel
        void Close() override {
            std::unique_lock<std::mutex> lock(mtx);
            this->closed_ = true;
            cv.notify_all();
        }
    };
}
