#pragma once

#include <optional>
#include <vector>

#include "Channel.hpp"
#include "ChannelCore.hpp"

namespace QTrading::Utils::Queue {

    /// \brief Shared blocking receive/close/size logic for mutex-protected channels.
    template <typename T, typename Derived>
    class BlockingChannelBase : public Channel<T> {
    protected:
        enum class SendMode {
            Block,
            Try
        };

        ChannelCore core_;

        Derived& self() { return static_cast<Derived&>(*this); }
        const Derived& self() const { return static_cast<const Derived&>(*this); }

        bool SendWithMode(T value, SendMode mode) {
            std::unique_lock<std::mutex> lock(core_.mtx);
            if (this->closed_.load(std::memory_order_acquire)) return false;
            return self().SendLocked(std::move(value), lock, mode);
        }

    public:
        /// \copydoc Channel::Receive
        std::optional<T> Receive() override {
            std::unique_lock<std::mutex> lock(core_.mtx);
            core_.cv_not_empty.wait(lock, [this] {
                return this->closed_.load(std::memory_order_acquire) || self().HasDataLocked();
                });
            if (!self().HasDataLocked()) return std::nullopt;
            auto v = self().PopLocked();
            lock.unlock();
            if (v) {
                self().OnPopped(1, false);
            }
            return v;
        }

        /// \copydoc Channel::TryReceive
        std::optional<T> TryReceive() override {
            std::unique_lock<std::mutex> lock(core_.mtx);
            if (!self().HasDataLocked()) return std::nullopt;
            auto v = self().PopLocked();
            lock.unlock();
            if (v) {
                self().OnPopped(1, false);
            }
            return v;
        }

        /// \copydoc Channel::ReceiveMany
        std::vector<T> ReceiveMany(size_t max_items) override {
            std::vector<T> out;
            out.reserve(max_items);

            std::unique_lock<std::mutex> lock(core_.mtx);
            const size_t popped = self().PopManyLocked(max_items, out);
            lock.unlock();

            if (popped > 0) {
                self().OnPopped(popped, true);
            }
            return out;
        }

        /// \copydoc Channel::Close
        void Close() override {
            core_.Close(this->closed_);
        }

        /// \copydoc Channel::Size
        size_t Size() const override {
            std::lock_guard<std::mutex> lock(core_.mtx);
            return self().SizeLocked();
        }
    };

} // namespace QTrading::Utils::Queue
