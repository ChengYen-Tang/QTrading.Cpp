#pragma once

#include <arrow/api.h>
#include <memory>
#include <string>

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
                arrow::field("is_taker", arrow::boolean()),
                arrow::field("fee", arrow::float64()),
                arrow::field("fee_rate", arrow::float64()),
                arrow::field("reject_reason", arrow::int32())
            });
        }

        inline void Serializer(const void* src, arrow::RecordBatchBuilder& builder)
        {
            const auto& e = *static_cast<const OrderEventDto*>(src);

            builder.GetFieldAs<arrow::UInt64Builder>(1)->Append(e.run_id);
            builder.GetFieldAs<arrow::UInt64Builder>(2)->Append(e.step_seq);
            builder.GetFieldAs<arrow::UInt64Builder>(3)->Append(e.event_seq);
            builder.GetFieldAs<arrow::UInt64Builder>(4)->Append(e.request_id);
            builder.GetFieldAs<arrow::Int64Builder>(5)->Append(e.order_id);
            builder.GetFieldAs<arrow::StringBuilder>(6)->Append(e.symbol);
            builder.GetFieldAs<arrow::Int32Builder>(7)->Append(e.event_type);
            builder.GetFieldAs<arrow::Int32Builder>(8)->Append(e.side);
            builder.GetFieldAs<arrow::Int32Builder>(9)->Append(e.position_side);
            builder.GetFieldAs<arrow::BooleanBuilder>(10)->Append(e.reduce_only);
            builder.GetFieldAs<arrow::DoubleBuilder>(11)->Append(e.qty);
            builder.GetFieldAs<arrow::DoubleBuilder>(12)->Append(e.price);
            builder.GetFieldAs<arrow::DoubleBuilder>(13)->Append(e.exec_qty);
            builder.GetFieldAs<arrow::DoubleBuilder>(14)->Append(e.exec_price);
            builder.GetFieldAs<arrow::DoubleBuilder>(15)->Append(e.remaining_qty);
            builder.GetFieldAs<arrow::BooleanBuilder>(16)->Append(e.is_taker);
            builder.GetFieldAs<arrow::DoubleBuilder>(17)->Append(e.fee);
            builder.GetFieldAs<arrow::DoubleBuilder>(18)->Append(e.fee_rate);
            builder.GetFieldAs<arrow::Int32Builder>(19)->Append(e.reject_reason);
        }
    } // namespace OrderEvent

} // namespace QTrading::Log::FileLogger::FeatherV2
