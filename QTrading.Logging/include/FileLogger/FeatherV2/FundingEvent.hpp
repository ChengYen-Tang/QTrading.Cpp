#pragma once

#include <arrow/api.h>
#include <memory>
#include <string>

#include "FileLogger/FeatherV2/ArrowAppend.hpp"

namespace QTrading::Log::FileLogger::FeatherV2 {

    struct FundingEventDto {
        uint64_t run_id{};
        uint64_t step_seq{};
        uint64_t event_seq{};
        uint64_t ts_local{};

        std::string symbol;
        int32_t instrument_type{ -1 }; // Dto::Trading::InstrumentType, -1 means unknown.
        uint64_t funding_time{};
        double rate{};
        bool has_mark_price{};
        double mark_price{};
        int64_t position_id{};
        bool is_long{};
        double quantity{};
        double funding{};
    };

    namespace FundingEvent {
        inline std::shared_ptr<arrow::Schema> Schema()
        {
            return arrow::schema({
                arrow::field("ts", arrow::uint64()),
                arrow::field("run_id", arrow::uint64()),
                arrow::field("step_seq", arrow::uint64()),
                arrow::field("event_seq", arrow::uint64()),
                arrow::field("symbol", arrow::utf8()),
                arrow::field("instrument_type", arrow::int32()),
                arrow::field("funding_time", arrow::uint64()),
                arrow::field("rate", arrow::float64()),
                arrow::field("has_mark_price", arrow::boolean()),
                arrow::field("mark_price", arrow::float64()),
                arrow::field("position_id", arrow::int64()),
                arrow::field("is_long", arrow::boolean()),
                arrow::field("quantity", arrow::float64()),
                arrow::field("funding", arrow::float64()),
                arrow::field("ts_local", arrow::uint64())
            });
        }

        inline void Serializer(const void* src, arrow::RecordBatchBuilder& builder)
        {
            const auto& e = *static_cast<const FundingEventDto*>(src);

            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(1), e.run_id);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(2), e.step_seq);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(3), e.event_seq);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::StringBuilder>(4), e.symbol);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int32Builder>(5), e.instrument_type);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(6), e.funding_time);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(7), e.rate);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::BooleanBuilder>(8), e.has_mark_price);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(9), e.mark_price);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int64Builder>(10), e.position_id);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::BooleanBuilder>(11), e.is_long);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(12), e.quantity);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(13), e.funding);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(14), e.ts_local);
        }
    } // namespace FundingEvent

} // namespace QTrading::Log::FileLogger::FeatherV2
