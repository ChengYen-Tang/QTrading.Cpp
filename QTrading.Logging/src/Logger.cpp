#include "Logger.hpp"
#include <filesystem>

namespace fs = std::filesystem;
using QTrading::Utils::Queue::ChannelFactory;

namespace QTrading::Log {
	Logger::Logger(const std::string& dir)
		: dir(dir)
	{
		fs::create_directories(dir);
	}

    /* 啟動 Consumer */
    void Logger::Start()
    {
        std::lock_guard lk(mtx);
        if (channel) return;  // already started

        channel.reset(ChannelFactory::CreateUnboundedChannel<Row>());
        consumer = boost::thread(&Logger::Consume, this);
    }

    /* 停止 Consumer */
    void Logger::Stop()
    {
        {
            std::lock_guard lk(mtx);
            if (!channel) return;
            channel->Close();
        }
        consumer.join();
        channel.reset();
    }
}
