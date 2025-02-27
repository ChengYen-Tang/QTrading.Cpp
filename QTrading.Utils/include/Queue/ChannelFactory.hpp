#pragma once

#include "BoundedChannel.hpp"
#include "UnBoundedChannel.hpp"

namespace QTrading::Utils::Queue
{
    class ChannelFactory {
    public:
        template <typename T>
        static Channel<T>* CreateBoundedChannel(size_t capacity, OverflowPolicy policy = OverflowPolicy::Block) {
            return new BoundedChannel<T>(capacity, policy);
        }

        template <typename T>
        static Channel<T>* CreateUnboundedChannel() {
            return new UnboundedChannel<T>();
        }
    };
}
