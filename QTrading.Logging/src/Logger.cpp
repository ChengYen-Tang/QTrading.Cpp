#include "Logger.hpp"
#include <filesystem>

namespace fs = std::filesystem;
using QTrading::Utils::Queue::ChannelFactory;

namespace QTrading::Log {

    /// @brief Create logger and ensure the directory exists.
    /// @param dir Directory for log files.
    Logger::Logger(const std::string& dir)
        : dir(dir)
    {
        fs::create_directories(dir);
    }

    /// @brief Start the consumer thread and initialize the channel.
    /// No-op if already started.
    void Logger::Start()
    {
        std::lock_guard<std::mutex> lk(mtx);
        if (channel) {
            return;
        }
        channel.reset(ChannelFactory::CreateUnboundedChannel<Row>());
        consumer = boost::thread(&Logger::Consume, this);
    }

    /// @brief Stop the consumer thread, close channel, and join.
    /// No-op if not started.
    void Logger::Stop()
    {
        {
            std::lock_guard<std::mutex> lk(mtx);
            if (!channel) {
                return;
            }
            channel->Close();
        }
        consumer.join();
        channel.reset();
    }
}
