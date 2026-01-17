#pragma once

#include <mutex>
#include <optional>
#include <vector>

#include "BlockingChannelBase.hpp"
#include "Channel.hpp"
#include "ChannelCore.hpp"
#include "ChunkedQueue.hpp"
#include "QueueOps.hpp"

namespace QTrading::Utils::Queue {

    /// \brief A thread-safe channel with unbounded capacity.
    /// \tparam T Message type.
    /// \remarks Messages accumulate without limit until Close() is called.
    template <typename T>
    class UnboundedChannel : public BlockingChannelBase<T, UnboundedChannel<T>> {
    public:
        using Base = BlockingChannelBase<T, UnboundedChannel<T>>;
        using SendMode = typename Base::SendMode;

        explicit UnboundedChannel(size_t block_capacity = 1024)
            : queue_(block_capacity) {
        }

        /// \copydoc Channel::Send
        bool Send(T value) override {
            return this->SendWithMode(std::move(value), SendMode::Block);
        }

        /// \copydoc Channel::TrySend
        bool TrySend(T value) override {
            return Send(std::move(value));
        }

    private:
        friend class BlockingChannelBase<T, UnboundedChannel<T>>;

        bool SendLocked(T value, std::unique_lock<std::mutex>& lock, SendMode /*mode*/) {
            QueueOps<ChunkedQueue<T>>::Push(queue_, std::move(value));
            lock.unlock();
            this->core_.cv_not_empty.notify_one();
            return true;
        }

        bool HasDataLocked() const {
            return !QueueOps<ChunkedQueue<T>>::Empty(queue_);
        }

        std::optional<T> PopLocked() {
            return QueueOps<ChunkedQueue<T>>::Pop(queue_);
        }

        size_t PopManyLocked(size_t max_items, std::vector<T>& out) {
            return QueueOps<ChunkedQueue<T>>::PopMany(queue_, max_items, out);
        }

        size_t SizeLocked() const {
            return QueueOps<ChunkedQueue<T>>::Size(queue_);
        }

        void OnPopped(size_t /*count*/, bool /*from_receive_many*/) {}

        ChunkedQueue<T> queue_;
    };

} // namespace QTrading::Utils::Queue
