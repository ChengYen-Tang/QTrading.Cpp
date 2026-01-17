#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace QTrading::Utils::Queue {

    /// \brief Shared synchronization core for channel implementations.
    struct ChannelCore {
        mutable std::mutex mtx;
        std::condition_variable cv_not_empty;
        std::condition_variable cv_not_full;

        void Close(std::atomic<bool>& closed) {
            std::lock_guard<std::mutex> lock(mtx);
            closed.store(true, std::memory_order_release);
            cv_not_empty.notify_all();
            cv_not_full.notify_all();
        }
    };

} // namespace QTrading::Utils::Queue
