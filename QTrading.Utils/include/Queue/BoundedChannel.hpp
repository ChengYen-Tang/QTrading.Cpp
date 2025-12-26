#pragma once

#include <condition_variable>
#include <mutex>
#include <vector>

#include "Channel.hpp"
#include "RingBuffer.hpp"

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
            : buffer_(capacity), policy_(policy) {
        }

        /// \copydoc Channel::Send
        bool Send(T value) override {
            std::unique_lock<std::mutex> lock(mtx_);
            if (this->closed_.load(std::memory_order_acquire)) return false;

            if (!buffer_.full()) {
                buffer_.push(std::move(value));
                lock.unlock();
                cv_not_empty_.notify_one();
                return true;
            }

            switch (policy_) {
            case OverflowPolicy::Reject:
                return false;
            case OverflowPolicy::DropOldest:
                buffer_.drop_oldest();
                buffer_.push(std::move(value));
                lock.unlock();
                cv_not_empty_.notify_one();
                return true;
            case OverflowPolicy::Block:
            default:
                cv_not_full_.wait(lock, [&] {
                    return this->closed_.load(std::memory_order_acquire) || !buffer_.full();
                    });
                if (this->closed_.load(std::memory_order_acquire)) return false;
                buffer_.push(std::move(value));
                lock.unlock();
                cv_not_empty_.notify_one();
                return true;
            }
        }

        /// \copydoc Channel::TrySend
        bool TrySend(T value) override {
            std::unique_lock<std::mutex> lock(mtx_);
            if (this->closed_.load(std::memory_order_acquire)) return false;

            if (!buffer_.full()) {
                buffer_.push(std::move(value));
                lock.unlock();
                cv_not_empty_.notify_one();
                return true;
            }

            switch (policy_) {
            case OverflowPolicy::Reject:
                return false;
            case OverflowPolicy::DropOldest:
                buffer_.drop_oldest();
                buffer_.push(std::move(value));
                lock.unlock();
                cv_not_empty_.notify_one();
                return true;
            case OverflowPolicy::Block:
            default:
                return false; // would block
            }
        }

        /// \copydoc Channel::Receive
        std::optional<T> Receive() override {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_not_empty_.wait(lock, [&] {
                return !buffer_.empty() || this->closed_.load(std::memory_order_acquire);
                });

            if (buffer_.empty()) return std::nullopt;

            auto v = buffer_.pop();
            lock.unlock();
            cv_not_full_.notify_one();
            return v;
        }

        /// \copydoc Channel::TryReceive
        std::optional<T> TryReceive() override {
            std::unique_lock<std::mutex> lock(mtx_);
            if (buffer_.empty()) return std::nullopt;
            auto v = buffer_.pop();
            lock.unlock();
            cv_not_full_.notify_one();
            return v;
        }

        /// \copydoc Channel::ReceiveMany
        std::vector<T> ReceiveMany(size_t max_items) override {
            std::vector<T> out;
            out.reserve(max_items);

            std::unique_lock<std::mutex> lock(mtx_);
            const auto n = (std::min)(max_items, buffer_.size());
            for (size_t i = 0; i < n; ++i) {
                auto v = buffer_.pop();
                if (!v) break;
                out.push_back(std::move(*v));
            }
            lock.unlock();

            if (!out.empty()) {
                cv_not_full_.notify_all();
            }
            return out;
        }

        /// \copydoc Channel::Close
        void Close() override {
            {
                std::lock_guard<std::mutex> lock(mtx_);
                this->closed_.store(true, std::memory_order_release);
            }
            cv_not_empty_.notify_all();
            cv_not_full_.notify_all();
        }

    private:
        RingBuffer<T> buffer_;
        OverflowPolicy policy_;
        std::mutex mtx_;
        std::condition_variable cv_not_empty_;
        std::condition_variable cv_not_full_;
    };

} // namespace QTrading::Utils::Queue
