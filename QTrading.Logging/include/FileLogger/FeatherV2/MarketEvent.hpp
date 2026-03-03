#pragma once

#include <arrow/api.h>
#include <memory>
#include <string>

#include "FileLogger/FeatherV2/ArrowAppend.hpp"

namespace QTrading::Log::FileLogger::FeatherV2 {

    struct MarketEventDto {
        uint64_t run_id{};
        uint64_t step_seq{};
        uint64_t event_seq{};
        uint64_t ts_local{};

        std::string symbol;
        bool has_kline{};

        double open{};
        double high{};
        double low{};
        double close{};
        double volume{};
        double taker_buy_base_volume{};
    };

    namespace MarketEvent {
        inline std::shared_ptr<arrow::Schema> Schema()
        {
            return arrow::schema({
                arrow::field("ts", arrow::uint64()),
                arrow::field("run_id", arrow::uint64()),
                arrow::field("step_seq", arrow::uint64()),
                arrow::field("event_seq", arrow::uint64()),
                arrow::field("symbol", arrow::utf8()),
                arrow::field("has_kline", arrow::boolean()),
                arrow::field("open", arrow::float64()),
                arrow::field("high", arrow::float64()),
                arrow::field("low", arrow::float64()),
                arrow::field("close", arrow::float64()),
                arrow::field("volume", arrow::float64()),
                arrow::field("taker_buy_base_volume", arrow::float64()),
                arrow::field("ts_local", arrow::uint64())
            });
        }

        inline void Serializer(const void* src, arrow::RecordBatchBuilder& builder)
        {
            const auto& e = *static_cast<const MarketEventDto*>(src);

            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(1), e.run_id);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(2), e.step_seq);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(3), e.event_seq);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::StringBuilder>(4), e.symbol);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::BooleanBuilder>(5), e.has_kline);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(6), e.open);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(7), e.high);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(8), e.low);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(9), e.close);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(10), e.volume);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(11), e.taker_buy_base_volume);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(12), e.ts_local);
        }
    } // namespace MarketEvent

} // namespace QTrading::Log::FileLogger::FeatherV2
