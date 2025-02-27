#pragma once

#include <optional>

namespace QTrading::Utils::Queue
{
    template <typename T>
    class Channel {
    protected:
        bool closed_ = false;
    public:
        // Send a value to the channel
        virtual bool Send(T value) = 0;
        // Receive a value from the channel
        virtual std::optional<T> Receive() = 0;
        // Try to receive a value from the channel without blocking
        virtual std::optional<T> TryReceive() = 0;
        // Close the channel
        virtual void Close() = 0;
		bool IsClosed() const { return closed_; }
    };
}
