#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include "Channel.hpp"

namespace QTrading::Utils::Queue
{
    // Block: block the sender until there is space in the queue
    // DropOldest: drop the oldest element in the queue to make space for the new element
    // Reject: reject the new element if the queue is full
    enum class OverflowPolicy {
        Block,
        DropOldest,
        Reject
    };

    // BoundedChannel is a thread-safe channel with a fixed capacity
    template <typename T>
    class BoundedChannel : public Channel<T> {
    private:
        std::queue<T> queue_;
        size_t capacity_;
        OverflowPolicy policy_;

        std::mutex mtx_;

        std::condition_variable cv_not_empty_;
        std::condition_variable cv_not_full_;

    public:
        // Create a new BoundedChannel with the given capacity and overflow policy
        // The default overflow policy is Block
        // The capacity must be greater than 0
        explicit BoundedChannel(size_t capacity, OverflowPolicy policy = OverflowPolicy::Block)
            : capacity_(capacity), policy_(policy) {
        }

        // Send a value to the channel
        bool Send(T value) override {
            std::unique_lock<std::mutex> lock(mtx_);

            if (this->closed_) {
                return false;
            }

            if (queue_.size() < capacity_) {
                queue_.push(std::move(value));
                cv_not_empty_.notify_one();
                return true;
            }

            switch (policy_) {
            case OverflowPolicy::Reject:
                return false;

            case OverflowPolicy::DropOldest:
                if (!queue_.empty()) {
                    queue_.pop();
                }
                queue_.push(std::move(value));
                cv_not_empty_.notify_one();
                return true;

            case OverflowPolicy::Block:
            default:
                while (!this->closed_ && queue_.size() == capacity_) {
                    cv_not_full_.wait(lock);
                }
                if (this->closed_) {
                    return false;
                }
                queue_.push(std::move(value));
                cv_not_empty_.notify_one();
                return true;
            }
        }

        // Receive a value from the channel
        std::optional<T> Receive() override {
            std::unique_lock<std::mutex> lock(mtx_);

            while (queue_.empty() && !this->closed_) {
                cv_not_empty_.wait(lock);
            }
            if (queue_.empty()) {
                return std::nullopt;
            }

            T frontVal = std::move(queue_.front());
            queue_.pop();

            cv_not_full_.notify_one();
            return frontVal;
        }

        // Try to receive a value from the channel without blocking
        std::optional<T> TryReceive() override {
            std::lock_guard<std::mutex> lock(mtx_);
            if (queue_.empty()) {
                return std::nullopt;
            }
            T frontVal = std::move(queue_.front());
            queue_.pop();
            cv_not_full_.notify_one();
            return frontVal;
        }

        // Close the channel
        void Close() override {
            std::unique_lock<std::mutex> lock(mtx_);
            this->closed_ = true;
            cv_not_empty_.notify_all();
            cv_not_full_.notify_all();
        }
    };
}
