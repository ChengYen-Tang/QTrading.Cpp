#pragma once

#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace QTrading::Utils::Queue {

    template <typename T>
    class RingBuffer {
    public:
        explicit RingBuffer(size_t capacity)
            : capacity_(capacity), data_(capacity) {
        }

        size_t capacity() const noexcept { return capacity_; }
        size_t size() const noexcept { return size_; }
        bool empty() const noexcept { return size_ == 0; }
        bool full() const noexcept { return size_ == capacity_; }

        bool push(T value) {
            if (full()) return false;
            data_[tail_] = std::move(value);
            tail_ = (tail_ + 1) % capacity_;
            ++size_;
            return true;
        }

        std::optional<T> pop() {
            if (empty()) return std::nullopt;
            T v = std::move(data_[head_]);
            head_ = (head_ + 1) % capacity_;
            --size_;
            return v;
        }

        bool pop_into(T& out) {
            if (empty()) return false;
            out = std::move(data_[head_]);
            head_ = (head_ + 1) % capacity_;
            --size_;
            return true;
        }

        bool drop_oldest() {
            if (empty()) return false;
            head_ = (head_ + 1) % capacity_;
            --size_;
            return true;
        }

    private:
        size_t capacity_{ 0 };
        std::vector<T> data_;
        size_t head_{ 0 };
        size_t tail_{ 0 };
        size_t size_{ 0 };
    };

} // namespace QTrading::Utils::Queue
