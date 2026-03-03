#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace QTrading::Utils::Queue {

    /// \brief A cache-friendly unbounded queue using fixed-size blocks.
    /// \details Push/pop are O(1) and avoid per-element allocations.
    template <typename T>
    class ChunkedQueue {
    public:
        explicit ChunkedQueue(size_t block_capacity = 1024)
            : block_capacity_(block_capacity) {
            if (block_capacity_ == 0) block_capacity_ = 1;
        }

        bool empty() const noexcept { return size_ == 0; }
        size_t size() const noexcept { return size_; }

        void push(T value) {
            if (!tail_ || tail_->end == block_capacity_) {
                append_block();
            }
            tail_->data[tail_->end++] = std::move(value);
            ++size_;
        }

        std::optional<T> pop() {
            if (size_ == 0) return std::nullopt;
            T v = std::move(head_->data[head_->begin++]);
            --size_;
            maybe_release_head();
            return v;
        }

        template <class OutputIt>
        size_t pop_many(size_t max_items, OutputIt out) {
            size_t popped = 0;
            while (popped < max_items && size_ != 0) {
                *out++ = std::move(head_->data[head_->begin++]);
                ++popped;
                --size_;
                maybe_release_head();
            }
            return popped;
        }

    private:
        struct Block {
            explicit Block(size_t cap) : data(cap) {}
            std::vector<T> data;
            size_t begin{ 0 };
            size_t end{ 0 };
            std::unique_ptr<Block> next;
        };

        void append_block() {
            auto nb = std::make_unique<Block>(block_capacity_);
            nb->begin = 0;
            nb->end = 0;

            if (!head_) {
                head_ = std::move(nb);
                tail_ = head_.get();
                return;
            }

            tail_->next = std::move(nb);
            tail_ = tail_->next.get();
        }

        void maybe_release_head() {
            if (!head_) return;

            // If current head block is fully consumed and there's another block, advance.
            if (head_->begin == head_->end && head_->next) {
                head_ = std::move(head_->next);
                if (!head_->next) {
                    tail_ = head_.get();
                }
                return;
            }

            // If queue becomes empty and we're on the last block, reset indices for reuse.
            if (size_ == 0 && head_->begin == head_->end && !head_->next) {
                head_->begin = 0;
                head_->end = 0;
                tail_ = head_.get();
            }
        }

        size_t block_capacity_{ 1024 };
        std::unique_ptr<Block> head_;
        Block* tail_{ nullptr };
        size_t size_{ 0 };
    };

} // namespace QTrading::Utils::Queue
