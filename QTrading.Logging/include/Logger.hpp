#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <boost/thread.hpp>
#include "Global.hpp"
#include "Queue/ChannelFactory.hpp"

namespace QTrading::Log {
    /// @struct Row
    /// @brief Represents one log record for output.
    struct Row {
        std::string           module;   ///< Name of the logging module.
        unsigned long long    ts;       ///< Timestamp from GlobalTimestamp.
        std::shared_ptr<void> payload;  ///< Pointer to the log data object.
    };

    /// @class Logger
    /// @brief Abstract base for a singleton logger with a background consumer thread.
    class Logger {
    public:
        /// @brief Create a logger storing output in the given directory.
        /// @param dir Directory path for log files.
        explicit Logger(const std::string& dir);

        /// @brief Start the consumer thread and channel.
        virtual void Start();

        /// @brief Stop the consumer thread, flush pending logs, and join.
        virtual void Stop();

        /// @brief Send a log entry (non-blocking).
        /// @tparam T   Payload type.
        /// @param module Module name.
        /// @param obj   Shared pointer to payload.
        template<typename T>
        inline void Log(const std::string& module,
            std::shared_ptr<T> obj) noexcept
        {
            Row r{
                module,
                QTrading::Utils::GlobalTimestamp.load(std::memory_order_relaxed),
                std::static_pointer_cast<void>(std::move(obj))
            };
            channel->Send(std::move(r));
        }

    protected:
        std::string dir;  ///< Output directory for log files.

        /// @brief Called by the background thread to consume Rows.
        virtual void Consume() = 0;

        std::shared_ptr<QTrading::Utils::Queue::Channel<Row>> channel; ///< Internal channel.
        boost::thread                                         consumer;///< Consumer thread.
        std::mutex                                            mtx;     ///< Protects start/stop.
    };
}
