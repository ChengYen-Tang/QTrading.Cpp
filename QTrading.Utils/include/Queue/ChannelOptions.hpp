#pragma once

#include <cstddef>

namespace QTrading::Utils::Queue {

    /// \brief Options to select channel implementation optimizations.
    struct ChannelOptions {
        bool   single_reader = false;   ///< Only one consumer thread.
        bool   single_writer = false;   ///< Only one producer thread.
        size_t block_capacity = 1024;   ///< Block size for chunked queues.
    };

} // namespace QTrading::Utils::Queue
