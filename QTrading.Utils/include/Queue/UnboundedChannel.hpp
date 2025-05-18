#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include "Channel.hpp"

namespace QTrading::Utils::Queue {

    /// \brief A thread-safe channel with unbounded capacity.
    /// \tparam T Message type.
    /// \remarks Messages accumulate without limit until Close() is called.
    template <typename T>
    class UnboundedChannel : public Channel<T> {
    public:
        /// \copydoc Channel::Send
        bool Send(T value) override {
            std::unique_lock<std::mutex> lock(mtx_);
            if (this->closed_) return false;
            queue_.push(std::move(value));
            lock.unlock();
            cv_.notify_one();
            return true;
        }

        /// \copydoc Channel::Receive
        std::optional<T> Receive() override {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait(lock, [this] { return !queue_.empty() || this->closed_; });
            if (queue_.empty()) return std::nullopt;
            T front = std::move(queue_.front());
            queue_.pop();
            return front;
        }

        /// \copydoc Channel::TryReceive
        std::optional<T> TryReceive() override {
            std::lock_guard<std::mutex> lock(mtx_);
            if (queue_.empty()) return std::nullopt;
            T front = std::move(queue_.front());
            queue_.pop();
            return front;
        }

        /// \copydoc Channel::Close
        void Close() override {
            std::lock_guard<std::mutex> lock(mtx_);
            this->closed_ = true;
            cv_.notify_all();
        }

    private:
        std::queue<T> queue_;            ///< Underlying unbounded queue.
        std::mutex mtx_;
        std::condition_variable cv_;     ///< Signals when queue has data or is closed.
    };

} // namespace QTrading::Utils::Queue
