#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "LogSink.hpp"

namespace QTrading::Log {

    class SinkLogger : public Logger {
    public:
        explicit SinkLogger(const std::string& dir);

        void AddSink(std::unique_ptr<ILogSink> sink);

        void RegisterModule(const std::string& module,
            std::shared_ptr<arrow::Schema> schema,
            Serializer serializer,
            ChannelKind kind = ChannelKind::Critical);

    protected:
        void Consume() override;

    private:
        std::vector<std::unique_ptr<ILogSink>> sinks_;
    };

} // namespace QTrading::Log
