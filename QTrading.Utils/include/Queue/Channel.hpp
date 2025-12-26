#pragma once

#include <atomic>
#include <optional>
#include <vector>

namespace QTrading::Utils::Queue {

    /// \brief Abstract base class for a thread-safe message channel.
    /// \tparam T Type of messages passed through the channel.
    /// \remarks Implementations must support sending, blocking receive, non-blocking try-receive, and closing.
    template <typename T>
    class Channel {
    protected:
        std::atomic<bool> closed_{ false }; ///< Indicates if the channel has been closed.

    public:
        virtual ~Channel() = default;

        /// \brief Sends a value into the channel.
        /// \param value The message to send.
        /// \return true if the message was accepted, false if the channel is closed or the send was rejected.
        virtual bool Send(T value) = 0;

        /// \brief Attempts to send without blocking.
        /// \return true if accepted, false if channel is closed or would block.
        /// \remarks Default implementation forwards to Send().
        virtual bool TrySend(T value) { return Send(std::move(value)); }

        /// \brief Convenience function alias for Send().
        bool Push(T value) { return Send(std::move(value)); }

        /// \brief Convenience function alias for TrySend().
        bool TryPush(T value) { return TrySend(std::move(value)); }

        /// \brief Receives a value from the channel, blocking until available or closed.
        /// \return An optional containing the received message, or std::nullopt if the channel is closed and empty.
        virtual std::optional<T> Receive() = 0;

        /// \brief Attempts to receive a value without blocking.
        /// \return An optional containing the message if available, or std::nullopt if none.
        virtual std::optional<T> TryReceive() = 0;

        /// \brief Batch receive up to max_items without blocking.
        /// \param max_items Maximum items to receive.
        /// \return Vector containing received items (may be empty).
        /// \remarks Default implementation repeatedly calls TryReceive(); concrete channels should override for efficiency.
        virtual std::vector<T> ReceiveMany(size_t max_items) {
            std::vector<T> out;
            out.reserve(max_items);
            for (size_t i = 0; i < max_items; ++i) {
                auto v = TryReceive();
                if (!v) break;
                out.push_back(std::move(*v));
            }
            return out;
        }

        /// \brief Closes the channel, unblocking any waiting operations.
        /// \remarks After closing, Send() calls will return false and Receive()/TryReceive() will return any remaining messages before emptying.
        virtual void Close() = 0;

        /// \brief Checks whether the channel is closed.
        /// \return true if closed, false otherwise.
        bool IsClosed() const { return closed_.load(std::memory_order_acquire); }
    };

} // namespace QTrading::Utils::Queue
