#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <optional>
#include <vector>

#include "ChunkedQueue.hpp"
#include "RingBuffer.hpp"

namespace QTrading::Utils::Queue {

    template <typename QueueT>
    struct QueueOps;

    template <typename T>
    struct QueueOps<ChunkedQueue<T>> {
        static bool Empty(const ChunkedQueue<T>& queue) { return queue.empty(); }
        static size_t Size(const ChunkedQueue<T>& queue) { return queue.size(); }
        static void Push(ChunkedQueue<T>& queue, T value) { queue.push(std::move(value)); }
        static std::optional<T> Pop(ChunkedQueue<T>& queue) { return queue.pop(); }

        static size_t PopMany(ChunkedQueue<T>& queue, size_t max_items, std::vector<T>& out) {
            return queue.pop_many(max_items, std::back_inserter(out));
        }
    };

    template <typename T>
    struct QueueOps<RingBuffer<T>> {
        static bool Empty(const RingBuffer<T>& queue) { return queue.empty(); }
        static size_t Size(const RingBuffer<T>& queue) { return queue.size(); }
        static bool Full(const RingBuffer<T>& queue) { return queue.full(); }
        static bool Push(RingBuffer<T>& queue, T value) { return queue.push(std::move(value)); }
        static std::optional<T> Pop(RingBuffer<T>& queue) { return queue.pop(); }
        static bool DropOldest(RingBuffer<T>& queue) { return queue.drop_oldest(); }

        static size_t PopMany(RingBuffer<T>& queue, size_t max_items, std::vector<T>& out) {
            const size_t n = (std::min)(max_items, queue.size());
            for (size_t i = 0; i < n; ++i) {
                auto v = queue.pop();
                if (!v) break;
                out.push_back(std::move(*v));
            }
            return out.size();
        }
    };

    template <typename QueueT>
    struct BoundedQueueOps;

    template <typename T>
    struct BoundedQueueOps<RingBuffer<T>> {
        static bool Full(const RingBuffer<T>& queue) { return QueueOps<RingBuffer<T>>::Full(queue); }
        static bool Push(RingBuffer<T>& queue, T value) { return QueueOps<RingBuffer<T>>::Push(queue, std::move(value)); }
        static bool DropOldest(RingBuffer<T>& queue, std::atomic<uint64_t>& drop_count) {
            const bool ok = QueueOps<RingBuffer<T>>::DropOldest(queue);
            if (ok) {
                drop_count.fetch_add(1, std::memory_order_relaxed);
            }
            return ok;
        }
        static void RecordDrop(std::atomic<uint64_t>& drop_count) {
            drop_count.fetch_add(1, std::memory_order_relaxed);
        }
        static size_t Depth(const RingBuffer<T>& queue) { return QueueOps<RingBuffer<T>>::Size(queue); }
    };

} // namespace QTrading::Utils::Queue
