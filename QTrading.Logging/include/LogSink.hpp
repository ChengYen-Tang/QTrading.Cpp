#pragma once

#include <memory>
#include <string>

#include "FileLogger/FeatherV2.hpp"
#include "Logger.hpp"

namespace QTrading::Log {

    class ILogSink {
    public:
        virtual ~ILogSink() = default;

        virtual void RegisterModule(Logger::ModuleId module_id,
            const std::string& module,
            const std::shared_ptr<arrow::Schema>& schema,
            const Serializer& serializer) = 0;

        // Returns number of flushes performed as a result of this row.
        virtual uint64_t WriteRow(const Row& row) = 0;

        // Flush any buffered rows and return number of flushes performed.
        virtual uint64_t Flush() = 0;

        virtual void Close() = 0;
    };

} // namespace QTrading::Log
