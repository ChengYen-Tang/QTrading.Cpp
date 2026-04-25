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
        int32_t instrument_type{ -1 }; // Dto::Trading::InstrumentType, -1 means unknown.
        int32_t event_type{}; // OrderEventType

        int32_t side{};          // Dto::Trading::OrderSide (int)
        int32_t position_side{}; // Dto::Trading::PositionSide (int)
        bool reduce_only{};
        bool close_position{};
        double quote_order_qty{};

        double qty{};
        double price{};          // order price (0 => market)

        double exec_qty{};
        double exec_price{};

        double remaining_qty{};
        int64_t closing_position_id{};

        bool is_taker{};
        double fee{};
        double fee_rate{};
        int32_t fee_asset{ -1 };
        double fee_native{};
        double fee_quote_equiv{};
        double spot_cash_delta{};
        double spot_inventory_delta{};
        int32_t commission_model_source{ -1 };

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
                arrow::field("instrument_type", arrow::int32()),
                arrow::field("event_type", arrow::int32()),
                arrow::field("side", arrow::int32()),
                arrow::field("position_side", arrow::int32()),
                arrow::field("reduce_only", arrow::boolean()),
                arrow::field("close_position", arrow::boolean()),
                arrow::field("quote_order_qty", arrow::float64()),
                arrow::field("qty", arrow::float64()),
                arrow::field("price", arrow::float64()),
                arrow::field("exec_qty", arrow::float64()),
                arrow::field("exec_price", arrow::float64()),
                arrow::field("remaining_qty", arrow::float64()),
                arrow::field("closing_position_id", arrow::int64()),
                arrow::field("is_taker", arrow::boolean()),
                arrow::field("fee", arrow::float64()),
                arrow::field("fee_rate", arrow::float64()),
                arrow::field("fee_asset", arrow::int32()),
                arrow::field("fee_native", arrow::float64()),
                arrow::field("fee_quote_equiv", arrow::float64()),
                arrow::field("spot_cash_delta", arrow::float64()),
                arrow::field("spot_inventory_delta", arrow::float64()),
                arrow::field("commission_model_source", arrow::int32()),
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
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int32Builder>(7), e.instrument_type);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int32Builder>(8), e.event_type);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int32Builder>(9), e.side);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int32Builder>(10), e.position_side);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::BooleanBuilder>(11), e.reduce_only);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::BooleanBuilder>(12), e.close_position);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(13), e.quote_order_qty);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(14), e.qty);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(15), e.price);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(16), e.exec_qty);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(17), e.exec_price);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(18), e.remaining_qty);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int64Builder>(19), e.closing_position_id);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::BooleanBuilder>(20), e.is_taker);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(21), e.fee);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(22), e.fee_rate);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int32Builder>(23), e.fee_asset);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(24), e.fee_native);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(25), e.fee_quote_equiv);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(26), e.spot_cash_delta);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(27), e.spot_inventory_delta);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int32Builder>(28), e.commission_model_source);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int32Builder>(29), e.reject_reason);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(30), e.ts_local);
        }
    } // namespace OrderEvent

} // namespace QTrading::Log::FileLogger::FeatherV2
