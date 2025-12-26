#pragma once

#include <memory>

#include "BoundedChannel.hpp"
#include "UnboundedChannel.hpp"

namespace QTrading::Utils::Queue {

    /// \brief Factory for creating channel instances.
    /// \details Provides convenient static methods to create bounded or unbounded channels.
    class ChannelFactory {
    public:
        /// \brief Create a bounded channel managed by std::shared_ptr.
        template <typename T>
        static std::shared_ptr<Channel<T>> CreateBoundedChannel(size_t capacity, OverflowPolicy policy = OverflowPolicy::Block) {
            return std::make_shared<BoundedChannel<T>>(capacity, policy);
        }

        /// \brief Create an unbounded channel managed by std::shared_ptr.
        template <typename T>
        static std::shared_ptr<Channel<T>> CreateUnboundedChannel() {
            return std::make_shared<UnboundedChannel<T>>();
        }

        /// \brief Create an unbounded channel managed by std::shared_ptr with custom block capacity.
        template <typename T>
        static std::shared_ptr<Channel<T>> CreateUnboundedChannel(size_t block_capacity) {
            return std::make_shared<UnboundedChannel<T>>(block_capacity);
        }

        /// \brief Create a bounded channel managed by std::unique_ptr.
        template <typename T>
        static std::unique_ptr<Channel<T>> CreateBoundedChannelUnique(size_t capacity, OverflowPolicy policy = OverflowPolicy::Block) {
            return std::make_unique<BoundedChannel<T>>(capacity, policy);
        }

        /// \brief Create an unbounded channel managed by std::unique_ptr.
        template <typename T>
        static std::unique_ptr<Channel<T>> CreateUnboundedChannelUnique() {
            return std::make_unique<UnboundedChannel<T>>();
        }

        /// \brief Create an unbounded channel managed by std::unique_ptr with custom block capacity.
        template <typename T>
        static std::unique_ptr<Channel<T>> CreateUnboundedChannelUnique(size_t block_capacity) {
            return std::make_unique<UnboundedChannel<T>>(block_capacity);
        }
    };

} // namespace QTrading::Utils::Queue
