#pragma once

#include <atomic>
#include <mutex>
#include <vector>

#include "BlockingChannelBase.hpp"
#include "Channel.hpp"
#include "ChannelCore.hpp"
#include "QueueOps.hpp"
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
    class BoundedChannel : public BlockingChannelBase<T, BoundedChannel<T>> {
    public:
        using Base = BlockingChannelBase<T, BoundedChannel<T>>;
        using SendMode = typename Base::SendMode;

        /// \brief Construct a bounded channel.
        /// \param capacity Maximum number of messages.
        /// \param policy Overflow behavior when full.
        explicit BoundedChannel(size_t capacity, OverflowPolicy policy = OverflowPolicy::Block)
            : buffer_(capacity), policy_(policy) {
        }

        /// \copydoc Channel::Send
        bool Send(T value) override {
            return this->SendWithMode(std::move(value), SendMode::Block);
        }

        /// \copydoc Channel::TrySend
        bool TrySend(T value) override {
            return this->SendWithMode(std::move(value), SendMode::Try);
        }

        /// \copydoc Channel::DropCount
        uint64_t DropCount() const override {
            return drop_count_.load(std::memory_order_relaxed);
        }

    private:
        friend class BlockingChannelBase<T, BoundedChannel<T>>;

        bool SendLocked(T value, std::unique_lock<std::mutex>& lock, SendMode mode) {
            if (!BoundedQueueOps<RingBuffer<T>>::Full(buffer_)) {
                BoundedQueueOps<RingBuffer<T>>::Push(buffer_, std::move(value));
                lock.unlock();
                this->core_.cv_not_empty.notify_one();
                return true;
            }

            switch (policy_) {
            case OverflowPolicy::Reject:
                BoundedQueueOps<RingBuffer<T>>::RecordDrop(drop_count_);
                return false;
            case OverflowPolicy::DropOldest:
                BoundedQueueOps<RingBuffer<T>>::DropOldest(buffer_, drop_count_);
                BoundedQueueOps<RingBuffer<T>>::Push(buffer_, std::move(value));
                lock.unlock();
                this->core_.cv_not_empty.notify_one();
                return true;
            case OverflowPolicy::Block:
            default:
                if (mode == SendMode::Try) {
                    return false; // would block
                }
                this->core_.cv_not_full.wait(lock, [&] {
                    return this->closed_.load(std::memory_order_acquire) || !BoundedQueueOps<RingBuffer<T>>::Full(buffer_);
                    });
                if (this->closed_.load(std::memory_order_acquire)) return false;
                BoundedQueueOps<RingBuffer<T>>::Push(buffer_, std::move(value));
                lock.unlock();
                this->core_.cv_not_empty.notify_one();
                return true;
            }
        }

        bool HasDataLocked() const {
            return !QueueOps<RingBuffer<T>>::Empty(buffer_);
        }

        std::optional<T> PopLocked() {
            return QueueOps<RingBuffer<T>>::Pop(buffer_);
        }

        size_t PopManyLocked(size_t max_items, std::vector<T>& out) {
            return QueueOps<RingBuffer<T>>::PopMany(buffer_, max_items, out);
        }

        size_t SizeLocked() const {
            return BoundedQueueOps<RingBuffer<T>>::Depth(buffer_);
        }

        void OnPopped(size_t count, bool from_receive_many) {
            if (count == 0) return;
            if (from_receive_many) {
                this->core_.cv_not_full.notify_all();
            }
            else {
                this->core_.cv_not_full.notify_one();
            }
        }

        RingBuffer<T> buffer_;
        OverflowPolicy policy_;
        std::atomic<uint64_t> drop_count_{ 0 };
    };

} // namespace QTrading::Utils::Queue
