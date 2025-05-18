#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include "Channel.hpp"

namespace QTrading::Utils::Queue {

    /// \brief Policy to apply when a BoundedChannel is full.
    enum class OverflowPolicy {
        Block,      ///< Block sender until space is available.
        DropOldest, ///< Discard the oldest message to make space.
        Reject      ///< Reject new messages when full.
    };

    /// \brief A thread-safe channel with fixed capacity.
    /// \tparam T Message type.
    /// \details Supports configurable overflow policies for backpressure management.
    template <typename T>
    class BoundedChannel : public Channel<T> {
    public:
        /// \brief Construct a bounded channel.
        /// \param capacity Maximum number of messages.
        /// \param policy Overflow behavior when full.
        explicit BoundedChannel(size_t capacity, OverflowPolicy policy = OverflowPolicy::Block)
            : capacity_(capacity), policy_(policy) {
        }

        /// \copydoc Channel::Send
        bool Send(T value) override {
            std::unique_lock<std::mutex> lock(mtx_);
            if (this->closed_) return false;
            if (queue_.size() < capacity_) {
                queue_.push(std::move(value));
                cv_not_empty_.notify_one();
                return true;
            }
            switch (policy_) {
            case OverflowPolicy::Reject:
                return false;
            case OverflowPolicy::DropOldest:
                if (!queue_.empty()) queue_.pop();
                queue_.push(std::move(value));
                cv_not_empty_.notify_one();
                return true;
            case OverflowPolicy::Block:
            default:
                while (!this->closed_ && queue_.size() == capacity_) {
                    cv_not_full_.wait(lock);
                }
                if (this->closed_) return false;
                queue_.push(std::move(value));
                cv_not_empty_.notify_one();
                return true;
            }
        }

        /// \copydoc Channel::Receive
        std::optional<T> Receive() override {
            std::unique_lock<std::mutex> lock(mtx_);
            while (queue_.empty() && !this->closed_) {
                cv_not_empty_.wait(lock);
            }
            if (queue_.empty()) return std::nullopt;
            T front = std::move(queue_.front());
            queue_.pop();
            cv_not_full_.notify_one();
            return front;
        }

        /// \copydoc Channel::TryReceive
        std::optional<T> TryReceive() override {
            std::lock_guard<std::mutex> lock(mtx_);
            if (queue_.empty()) return std::nullopt;
            T front = std::move(queue_.front());
            queue_.pop();
            cv_not_full_.notify_one();
            return front;
        }

        /// \copydoc Channel::Close
        void Close() override {
            std::unique_lock<std::mutex> lock(mtx_);
            this->closed_ = true;
            cv_not_empty_.notify_all();
            cv_not_full_.notify_all();
        }

    private:
        std::queue<T> queue_;                   ///< Underlying message queue.
        size_t capacity_;                       ///< Maximum queue size.
        OverflowPolicy policy_;                 ///< Overflow handling strategy.
        std::mutex mtx_;
        std::condition_variable cv_not_empty_;  ///< Signals when queue is not empty.
        std::condition_variable cv_not_full_;   ///< Signals when queue is not full.
    };

} // namespace QTrading::Utils::Queue
