#pragma once

#include <arrow/api.h>
#include <memory>
#include <string>

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
                arrow::field("fee_rate", arrow::float64())
            });
        }

        inline void Serializer(const void* src, arrow::RecordBatchBuilder& builder)
        {
            const auto& e = *static_cast<const PositionEventDto*>(src);

            builder.GetFieldAs<arrow::UInt64Builder>(1)->Append(e.run_id);
            builder.GetFieldAs<arrow::UInt64Builder>(2)->Append(e.step_seq);
            builder.GetFieldAs<arrow::UInt64Builder>(3)->Append(e.event_seq);
            builder.GetFieldAs<arrow::UInt64Builder>(4)->Append(e.request_id);
            builder.GetFieldAs<arrow::Int64Builder>(5)->Append(e.source_order_id);
            builder.GetFieldAs<arrow::Int64Builder>(6)->Append(e.position_id);
            builder.GetFieldAs<arrow::StringBuilder>(7)->Append(e.symbol);
            builder.GetFieldAs<arrow::BooleanBuilder>(8)->Append(e.is_long);
            builder.GetFieldAs<arrow::Int32Builder>(9)->Append(e.event_type);
            builder.GetFieldAs<arrow::DoubleBuilder>(10)->Append(e.qty);
            builder.GetFieldAs<arrow::DoubleBuilder>(11)->Append(e.entry_price);
            builder.GetFieldAs<arrow::DoubleBuilder>(12)->Append(e.notional);
            builder.GetFieldAs<arrow::DoubleBuilder>(13)->Append(e.unrealized_pnl);
            builder.GetFieldAs<arrow::DoubleBuilder>(14)->Append(e.initial_margin);
            builder.GetFieldAs<arrow::DoubleBuilder>(15)->Append(e.maintenance_margin);
            builder.GetFieldAs<arrow::DoubleBuilder>(16)->Append(e.leverage);
            builder.GetFieldAs<arrow::DoubleBuilder>(17)->Append(e.fee);
            builder.GetFieldAs<arrow::DoubleBuilder>(18)->Append(e.fee_rate);
        }
    } // namespace PositionEvent

} // namespace QTrading::Log::FileLogger::FeatherV2
