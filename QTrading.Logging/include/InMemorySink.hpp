#pragma once

#include <vector>

#include "LogSink.hpp"

namespace QTrading::Log {

    class InMemorySink final : public ILogSink {
    public:
        void RegisterModule(Logger::ModuleId,
            const std::string&,
            const std::shared_ptr<arrow::Schema>&,
            const Serializer&) override {}

        uint64_t WriteRow(const Row& row) override
        {
            rows_.push_back(row);
            return 0;
        }

        uint64_t Flush() override { return 0; }

        void Close() override {}

        const std::vector<Row>& rows() const { return rows_; }

    private:
        std::vector<Row> rows_;
    };

} // namespace QTrading::Log
