#pragma once

#include <arrow/api.h>
#include <memory>
#include <string>

#include "FileLogger/FeatherV2/ArrowAppend.hpp"

namespace QTrading::Log::FileLogger::FeatherV2 {

    enum class OrderEventType : int32_t {
        Accepted = 0,
        Rejected = 1,
        Canceled = 2,
        Filled = 3
    };

    struct OrderEventDto {
        uint64_t run_id{};
        uint64_t step_seq{};
        uint64_t event_seq{};
        uint64_t ts_local{};

        uint64_t request_id{};

        int64_t order_id{};
        std::string symbol;
        int32_t event_type{}; // OrderEventType

        int32_t side{};          // Dto::Trading::OrderSide (int)
        int32_t position_side{}; // Dto::Trading::PositionSide (int)
        bool reduce_only{};

        double qty{};
        double price{};          // order price (0 => market)

        double exec_qty{};
        double exec_price{};

        double remaining_qty{};
        int64_t closing_position_id{};

        bool is_taker{};
        double fee{};
        double fee_rate{};

        int32_t reject_reason{}; // 0 = none
    };

    namespace OrderEvent {
        inline std::shared_ptr<arrow::Schema> Schema()
        {
            return arrow::schema({
                arrow::field("ts", arrow::uint64()),
                arrow::field("run_id", arrow::uint64()),
                arrow::field("step_seq", arrow::uint64()),
                arrow::field("event_seq", arrow::uint64()),
                arrow::field("request_id", arrow::uint64()),
                arrow::field("order_id", arrow::int64()),
                arrow::field("symbol", arrow::utf8()),
                arrow::field("event_type", arrow::int32()),
                arrow::field("side", arrow::int32()),
                arrow::field("position_side", arrow::int32()),
                arrow::field("reduce_only", arrow::boolean()),
                arrow::field("qty", arrow::float64()),
                arrow::field("price", arrow::float64()),
                arrow::field("exec_qty", arrow::float64()),
                arrow::field("exec_price", arrow::float64()),
                arrow::field("remaining_qty", arrow::float64()),
                arrow::field("closing_position_id", arrow::int64()),
                arrow::field("is_taker", arrow::boolean()),
                arrow::field("fee", arrow::float64()),
                arrow::field("fee_rate", arrow::float64()),
                arrow::field("reject_reason", arrow::int32()),
                arrow::field("ts_local", arrow::uint64())
            });
        }

        inline void Serializer(const void* src, arrow::RecordBatchBuilder& builder)
        {
            const auto& e = *static_cast<const OrderEventDto*>(src);

            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(1), e.run_id);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(2), e.step_seq);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(3), e.event_seq);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(4), e.request_id);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int64Builder>(5), e.order_id);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::StringBuilder>(6), e.symbol);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int32Builder>(7), e.event_type);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int32Builder>(8), e.side);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int32Builder>(9), e.position_side);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::BooleanBuilder>(10), e.reduce_only);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(11), e.qty);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(12), e.price);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(13), e.exec_qty);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(14), e.exec_price);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(15), e.remaining_qty);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int64Builder>(16), e.closing_position_id);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::BooleanBuilder>(17), e.is_taker);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(18), e.fee);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(19), e.fee_rate);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int32Builder>(20), e.reject_reason);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(21), e.ts_local);
        }
    } // namespace OrderEvent

} // namespace QTrading::Log::FileLogger::FeatherV2
