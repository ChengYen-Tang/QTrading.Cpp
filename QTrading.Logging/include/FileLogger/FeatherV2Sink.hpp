#pragma once

#include <memory>
#include <string>
#include <vector>

#include "LogSink.hpp"

namespace QTrading::Log::FileLogger {

    class FeatherV2Sink final : public ILogSink {
    public:
        explicit FeatherV2Sink(const std::string& dir);

        void RegisterModule(Logger::ModuleId module_id,
            const std::string& module,
            const std::shared_ptr<arrow::Schema>& schema,
            const Serializer& serializer) override;

        uint64_t WriteRow(const Row& row) override;

        uint64_t Flush() override;

        void Close() override;

    private:
        struct Slot {
            std::shared_ptr<arrow::Schema>                 schema;
            Serializer                                     serializer;
            std::unique_ptr<arrow::RecordBatchBuilder>     builder;
            std::shared_ptr<arrow::ipc::RecordBatchWriter> writer;
            std::shared_ptr<arrow::io::OutputStream>       outfile;
            uint32_t                                       rows = 0;
        };

        std::string dir_;
        std::vector<Slot> slots_;
    };

} // namespace QTrading::Log::FileLogger
