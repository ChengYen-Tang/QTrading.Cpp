#pragma once
#include <memory>
#include <mutex>
#include <string>

#include <boost/thread.hpp>

#include "Global.hpp"
#include "Queue/ChannelFactory.hpp"

namespace QTrading::Log {
    /* 一筆要寫入的記錄 */
    struct Row {
        std::string           module;   // 模組名稱
        unsigned long long    ts;       // GlobalTimestamp
        std::shared_ptr<void> payload;  // 指向具體 LogData
    };

    /* Singleton Logger */
    class Logger {
    public:
        Logger(const std::string& dir);
        /* 啟動 consumer thread & channel */
        virtual void Start();
        /* 停止，flush & join */
        virtual void Stop();

        /** 產生端呼叫：非阻塞、無序列化開銷  */
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
        std::string dir;

        virtual void Consume() = 0;

        /* 內部 Channel */
        std::shared_ptr<QTrading::Utils::Queue::Channel<Row>> channel;
        boost::thread                                         consumer;
        std::mutex                                            mtx;
    };
}
