#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <vector>

#include "BlockingChannelBase.hpp"
#include "Channel.hpp"
#include "ChannelCore.hpp"
#include "MpscQueue.hpp"

namespace QTrading::Utils::Queue {

    /// \brief A lock-free multi-producer, single-consumer channel with unbounded capacity.
    /// \tparam T Message type.
    /// \remarks Send is lock-free; Receive assumes a single consumer thread.
    template <typename T>
    class UnboundedMpscChannel : public BlockingChannelBase<T, UnboundedMpscChannel<T>> {
    public:
        explicit UnboundedMpscChannel(size_t /*block_capacity*/ = 1024) {
        }

        /// \copydoc Channel::Send
        bool Send(T value) override {
            if (this->closed_.load(std::memory_order_acquire)) {
                return false;
            }
            queue_.Push(std::move(value));
            this->core_.cv_not_empty.notify_one();
            return true;
        }

        /// \copydoc Channel::TrySend
        bool TrySend(T value) override {
            return Send(std::move(value));
        }

    private:
        friend class BlockingChannelBase<T, UnboundedMpscChannel<T>>;

        bool HasDataLocked() const {
            return queue_.HasData();
        }

        std::optional<T> PopLocked() {
            return queue_.TryPop();
        }

        size_t PopManyLocked(size_t max_items, std::vector<T>& out) {
            return queue_.PopMany(max_items, out);
        }

        size_t SizeLocked() const {
            return queue_.Size();
        }

        void OnPopped(size_t /*count*/, bool /*from_receive_many*/) {}

        MpscQueue<T> queue_;
    };

} // namespace QTrading::Utils::Queue
