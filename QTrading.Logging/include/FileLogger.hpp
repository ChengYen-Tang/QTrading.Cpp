#pragma once

#include <arrow/api.h>
#include <arrow/ipc/api.h>
#include <arrow/io/api.h>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

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

    /* Serializer：把你的 LogData 依 schema 寫進 RecordBatchBuilder */
    using Serializer = std::function<void(const void* src,
        arrow::RecordBatchBuilder& builder)>;

    /* Singleton Logger */
    class FileLogger {
    public:
        FileLogger(const std::string &dir);
        /* 啟動 consumer thread & channel */
        void Start();
        /* 停止，flush & join */
        void Stop();

        /**
         * 註冊模組：一次性呼叫
         *  - module   : 模組名稱 (同 Log())
         *  - schema   : Arrow schema (col-0 = uint64 ts)
         *  - serializer: 如何把 payload 寫進 RecordBatchBuilder
         */
        void RegisterModule(const std::string& module,
            std::shared_ptr<arrow::Schema> schema,
            Serializer serializer);

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
            channel_->Send(std::move(r));
        }

    private:
		std::string dir;
        FileLogger(const FileLogger&) = delete;
        FileLogger& operator=(const FileLogger&) = delete;

        /* 真正執行寫檔的 consumer loop */
        void Consume();

        /* 內部 Channel */
        std::shared_ptr<QTrading::Utils::Queue::Channel<Row>> channel_;
        boost::thread                                         consumer_;

        /* per-module 狀態 */
        struct Slot {
            std::shared_ptr<arrow::Schema>                    schema;
            Serializer                                        serializer;
            std::unique_ptr<arrow::RecordBatchBuilder>        builder;
            std::shared_ptr<arrow::ipc::RecordBatchWriter>    writer;
            std::shared_ptr<arrow::io::OutputStream>          outfile;
            uint32_t                                          rows = 0;
        };
        std::unordered_map<std::string, Slot> slots_;
        std::mutex                         mtx_;
    };
}
