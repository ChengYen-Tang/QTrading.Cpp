#pragma once

#include <optional>

namespace QTrading::Utils::Queue {

    /// \brief Abstract base class for a thread-safe message channel.
    /// \tparam T Type of messages passed through the channel.
    /// \remarks Implementations must support sending, blocking receive, non-blocking try-receive, and closing.
    template <typename T>
    class Channel {
    protected:
        bool closed_ = false; ///< Indicates if the channel has been closed.

    public:
        /// \brief Sends a value into the channel.
        /// \param value The message to send.
        /// \return true if the message was accepted, false if the channel is closed or the send was rejected.
        virtual bool Send(T value) = 0;

        /// \brief Receives a value from the channel, blocking until available or closed.
        /// \return An optional containing the received message, or std::nullopt if the channel is closed and empty.
        virtual std::optional<T> Receive() = 0;

        /// \brief Attempts to receive a value without blocking.
        /// \return An optional containing the message if available, or std::nullopt if none.
        virtual std::optional<T> TryReceive() = 0;

        /// \brief Closes the channel, unblocking any waiting operations.
        /// \remarks After closing, Send() calls will return false and Receive()/TryReceive() will return any remaining messages before emptying.
        virtual void Close() = 0;

        /// \brief Checks whether the channel is closed.
        /// \return true if closed, false otherwise.
        bool IsClosed() const { return closed_; }
    };

} // namespace QTrading::Utils::Queue
