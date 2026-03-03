#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <vector>

namespace QTrading::Utils::Queue {

    /// \brief Lock-free multi-producer, single-consumer queue core.
    template <typename T>
    class MpscQueue {
    public:
        MpscQueue() {
            auto* dummy = new Node();
            head_ = dummy;
            tail_.store(dummy, std::memory_order_relaxed);
        }

        ~MpscQueue() {
            DrainNodes();
        }

        void Push(T value) {
            auto* node = new Node(std::move(value));
            Node* prev = tail_.exchange(node, std::memory_order_acq_rel);
            prev->next.store(node, std::memory_order_release);
            size_.fetch_add(1, std::memory_order_relaxed);
        }

        bool HasData() const {
            return head_->next.load(std::memory_order_acquire) != nullptr;
        }

        std::optional<T> TryPop() {
            Node* head = head_;
            Node* next = head->next.load(std::memory_order_acquire);
            if (!next) {
                return std::nullopt;
            }
            std::optional<T> out = std::move(next->value);
            next->value.reset();
            head_ = next;
            delete head;
            size_.fetch_sub(1, std::memory_order_relaxed);
            return out;
        }

        size_t PopMany(size_t max_items, std::vector<T>& out) {
            size_t count = 0;
            for (size_t i = 0; i < max_items; ++i) {
                auto v = TryPop();
                if (!v) break;
                out.push_back(std::move(*v));
                ++count;
            }
            return count;
        }

        size_t Size() const {
            return size_.load(std::memory_order_relaxed);
        }

    private:
        struct Node {
            std::atomic<Node*> next{ nullptr };
            std::optional<T> value;

            Node() = default;
            explicit Node(T v) : value(std::move(v)) {}
        };

        void DrainNodes() {
            Node* node = head_;
            while (node) {
                Node* next = node->next.load(std::memory_order_relaxed);
                delete node;
                node = next;
            }
        }

        std::atomic<Node*> tail_{ nullptr };
        Node* head_{ nullptr };
        std::atomic<size_t> size_{ 0 };
    };

} // namespace QTrading::Utils::Queue
