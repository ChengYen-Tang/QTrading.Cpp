#pragma once

#include <arrow/api.h>
#include <memory>
#include <string>

#include "FileLogger/FeatherV2/ArrowAppend.hpp"

namespace QTrading::Log::FileLogger::FeatherV2 {

    struct RunMetadataDto {
        uint64_t run_id{};
        std::string strategy_name;
        std::string strategy_version;
        std::string strategy_params;
        std::string dataset;
    };

    namespace RunMetadata {
        inline std::shared_ptr<arrow::Schema> Schema()
        {
            return arrow::schema({
                arrow::field("ts", arrow::uint64()),
                arrow::field("run_id", arrow::uint64()),
                arrow::field("strategy_name", arrow::utf8()),
                arrow::field("strategy_version", arrow::utf8()),
                arrow::field("strategy_params", arrow::utf8()),
                arrow::field("dataset", arrow::utf8())
            });
        }

        inline void Serializer(const void* src, arrow::RecordBatchBuilder& builder)
        {
            const auto& e = *static_cast<const RunMetadataDto*>(src);

            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(1), e.run_id);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::StringBuilder>(2), e.strategy_name);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::StringBuilder>(3), e.strategy_version);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::StringBuilder>(4), e.strategy_params);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::StringBuilder>(5), e.dataset);
        }
    } // namespace RunMetadata

} // namespace QTrading::Log::FileLogger::FeatherV2
