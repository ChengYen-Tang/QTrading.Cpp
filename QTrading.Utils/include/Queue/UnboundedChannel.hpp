#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <vector>

#include "Channel.hpp"
#include "ChunkedQueue.hpp"

namespace QTrading::Utils::Queue {

    /// \brief A thread-safe channel with unbounded capacity.
    /// \tparam T Message type.
    /// \remarks Messages accumulate without limit until Close() is called.
    template <typename T>
    class UnboundedChannel : public Channel<T> {
    public:
        explicit UnboundedChannel(size_t block_capacity = 1024)
            : queue_(block_capacity) {
        }

        /// \copydoc Channel::Send
        bool Send(T value) override {
            std::unique_lock<std::mutex> lock(mtx_);
            if (this->closed_.load(std::memory_order_acquire)) return false;
            queue_.push(std::move(value));
            lock.unlock();
            cv_.notify_one();
            return true;
        }

        /// \copydoc Channel::TrySend
        bool TrySend(T value) override {
            return Send(std::move(value));
        }

        /// \copydoc Channel::Receive
        std::optional<T> Receive() override {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait(lock, [this] { return !queue_.empty() || this->closed_.load(std::memory_order_acquire); });
            if (queue_.empty()) return std::nullopt;
            return queue_.pop();
        }

        /// \copydoc Channel::TryReceive
        std::optional<T> TryReceive() override {
            std::unique_lock<std::mutex> lock(mtx_);
            if (queue_.empty()) return std::nullopt;
            return queue_.pop();
        }

        /// \copydoc Channel::ReceiveMany
        std::vector<T> ReceiveMany(size_t max_items) override {
            std::vector<T> out;
            out.reserve(max_items);

            std::unique_lock<std::mutex> lock(mtx_);
            queue_.pop_many(max_items, std::back_inserter(out));
            return out;
        }

        /// \copydoc Channel::Close
        void Close() override {
            {
                std::lock_guard<std::mutex> lock(mtx_);
                this->closed_.store(true, std::memory_order_release);
            }
            cv_.notify_all();
        }

    private:
        ChunkedQueue<T> queue_;
        std::mutex mtx_;
        std::condition_variable cv_;     ///< Signals when queue has data or is closed.
    };

} // namespace QTrading::Utils::Queue
