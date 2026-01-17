#include "SinkLogger.hpp"

#include <stdexcept>

namespace QTrading::Log {

    SinkLogger::SinkLogger(const std::string& dir)
        : Logger(dir)
    {
    }

    void SinkLogger::AddSink(std::unique_ptr<ILogSink> sink)
    {
        std::lock_guard<std::mutex> lk(mtx);
        if (channel) {
            throw std::runtime_error("AddSink must be called before Start().");
        }
        sinks_.push_back(std::move(sink));
    }

    void SinkLogger::RegisterModule(const std::string& module,
        std::shared_ptr<arrow::Schema> schema,
        Serializer serializer,
        ChannelKind kind)
    {
        std::lock_guard<std::mutex> lk(mtx);
        if (channel) {
            throw std::runtime_error("RegisterModule must be called before Start().");
        }
        if (GetModuleId(module) != kInvalidModuleId) {
            throw std::runtime_error("Module already registered: " + module);
        }

        const auto module_id = RegisterModuleId(module, kind);
        for (auto& sink : sinks_) {
            sink->RegisterModule(module_id, module, schema, serializer);
        }
    }

    void SinkLogger::Consume()
    {
        constexpr size_t kMaxBatch = 1024;
        std::vector<Row> batch;

        while (WaitForBatch(batch, kMaxBatch)) {
            for (const auto& row : batch) {
                for (auto& sink : sinks_) {
                    const auto flushed = sink->WriteRow(row);
                    if (flushed > 0) {
                        IncrementFlushCount(flushed);
                    }
                }
            }
        }

        for (auto& sink : sinks_) {
            const auto flushed = sink->Flush();
            if (flushed > 0) {
                IncrementFlushCount(flushed);
            }
            sink->Close();
        }
    }

} // namespace QTrading::Log
