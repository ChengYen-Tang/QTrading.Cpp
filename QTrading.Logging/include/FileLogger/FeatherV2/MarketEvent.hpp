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
        bool has_mark_price{};
        double mark_price{};
        int32_t mark_price_source{}; // 0=None, 1=Raw, 2=Interpolated
        bool has_index_price{};
        double index_price{};
        int32_t index_price_source{}; // 0=None, 1=Raw, 2=Interpolated
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
                arrow::field("has_mark_price", arrow::boolean()),
                arrow::field("mark_price", arrow::float64()),
                arrow::field("mark_price_source", arrow::int32()),
                arrow::field("has_index_price", arrow::boolean()),
                arrow::field("index_price", arrow::float64()),
                arrow::field("index_price_source", arrow::int32()),
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
            detail::AppendOrThrow(builder.GetFieldAs<arrow::BooleanBuilder>(12), e.has_mark_price);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(13), e.mark_price);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int32Builder>(14), e.mark_price_source);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::BooleanBuilder>(15), e.has_index_price);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(16), e.index_price);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int32Builder>(17), e.index_price_source);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(18), e.ts_local);
        }
    } // namespace MarketEvent

} // namespace QTrading::Log::FileLogger::FeatherV2
