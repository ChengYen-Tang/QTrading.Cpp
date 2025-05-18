#pragma once

#include "BoundedChannel.hpp"
#include "UnboundedChannel.hpp"

namespace QTrading::Utils::Queue {

    /// \brief Factory for creating channel instances.
    /// \details Provides convenient static methods to create bounded or unbounded channels.
    class ChannelFactory {
    public:
        /// \brief Create a new bounded channel.
        /// \tparam T Message type.
        /// \param capacity Maximum queue size.
        /// \param policy Overflow policy when full.
        /// \return Pointer to a new BoundedChannel<T>.
        template <typename T>
        static Channel<T>* CreateBoundedChannel(size_t capacity, OverflowPolicy policy = OverflowPolicy::Block) {
            return new BoundedChannel<T>(capacity, policy);
        }

        /// \brief Create a new unbounded channel.
        /// \tparam T Message type.
        /// \return Pointer to a new UnboundedChannel<T>.
        template <typename T>
        static Channel<T>* CreateUnboundedChannel() {
            return new UnboundedChannel<T>();
        }
    };

} // namespace QTrading::Utils::Queue
