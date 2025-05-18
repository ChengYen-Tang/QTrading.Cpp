#pragma once

#include <arrow/api.h>
#include <arrow/ipc/api.h>
#include <arrow/io/api.h>
#include <functional>
#include <unordered_map>
#include "Logger.hpp"

namespace QTrading::Log {
    /* Serializer：把你的 LogData 依 schema 寫進 RecordBatchBuilder */
    using Serializer = std::function<void(const void* src,
        arrow::RecordBatchBuilder& builder)>;

    /* Singleton Logger */
    class FeatherV2 : public Logger
    {
    public:
        FeatherV2(const std::string &dir);

        /**
         * 註冊模組：一次性呼叫
         *  - module   : 模組名稱 (同 Log())
         *  - schema   : Arrow schema (col-0 = uint64 ts)
         *  - serializer: 如何把 payload 寫進 RecordBatchBuilder
         */
        void RegisterModule(const std::string& module,
            std::shared_ptr<arrow::Schema> schema,
            Serializer serializer);
    protected:
        void Consume() override;
    private:
        FeatherV2(const FeatherV2&) = delete;
        FeatherV2& operator=(const FeatherV2&) = delete;

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
    };
}
