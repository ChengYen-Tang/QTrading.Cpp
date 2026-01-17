#pragma once

#include <arrow/api.h>
#include <memory>
#include <string>

#include "FileLogger/FeatherV2/ArrowAppend.hpp"

namespace QTrading::Log::FileLogger::FeatherV2 {

    enum class PositionEventType : int32_t {
        Snapshot = 0,
        Opened = 1,
        Increased = 2,
        Reduced = 3,
        Closed = 4
    };

    struct PositionEventDto {
        uint64_t run_id{};
        uint64_t step_seq{};
        uint64_t event_seq{};
        uint64_t ts_local{};

        uint64_t request_id{};
        int64_t source_order_id{};

        int64_t position_id{};
        std::string symbol;
        bool is_long{};

        int32_t event_type{};

        double qty{};
        double entry_price{};
        double notional{};
        double unrealized_pnl{};

        double initial_margin{};
        double maintenance_margin{};
        double leverage{};
        double fee{};
        double fee_rate{};
    };

    namespace PositionEvent {
        inline std::shared_ptr<arrow::Schema> Schema()
        {
            return arrow::schema({
                arrow::field("ts", arrow::uint64()),
                arrow::field("run_id", arrow::uint64()),
                arrow::field("step_seq", arrow::uint64()),
                arrow::field("event_seq", arrow::uint64()),
                arrow::field("request_id", arrow::uint64()),
                arrow::field("source_order_id", arrow::int64()),
                arrow::field("position_id", arrow::int64()),
                arrow::field("symbol", arrow::utf8()),
                arrow::field("is_long", arrow::boolean()),
                arrow::field("event_type", arrow::int32()),
                arrow::field("qty", arrow::float64()),
                arrow::field("entry_price", arrow::float64()),
                arrow::field("notional", arrow::float64()),
                arrow::field("unrealized_pnl", arrow::float64()),
                arrow::field("initial_margin", arrow::float64()),
                arrow::field("maintenance_margin", arrow::float64()),
                arrow::field("leverage", arrow::float64()),
                arrow::field("fee", arrow::float64()),
                arrow::field("fee_rate", arrow::float64()),
                arrow::field("ts_local", arrow::uint64())
            });
        }

        inline void Serializer(const void* src, arrow::RecordBatchBuilder& builder)
        {
            const auto& e = *static_cast<const PositionEventDto*>(src);

            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(1), e.run_id);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(2), e.step_seq);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(3), e.event_seq);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(4), e.request_id);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int64Builder>(5), e.source_order_id);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int64Builder>(6), e.position_id);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::StringBuilder>(7), e.symbol);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::BooleanBuilder>(8), e.is_long);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int32Builder>(9), e.event_type);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(10), e.qty);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(11), e.entry_price);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(12), e.notional);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(13), e.unrealized_pnl);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(14), e.initial_margin);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(15), e.maintenance_margin);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(16), e.leverage);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(17), e.fee);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(18), e.fee_rate);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(19), e.ts_local);
        }
    } // namespace PositionEvent

} // namespace QTrading::Log::FileLogger::FeatherV2
