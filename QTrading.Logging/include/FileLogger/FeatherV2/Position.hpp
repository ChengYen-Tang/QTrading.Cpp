#pragma once

#include "FileLogger/FeatherV2.hpp"
#include "Dto/Position.hpp"

namespace QTrading::Log::FileLogger::FeatherV2::Position {
    /// @brief Arrow schema for position logs.
    inline auto Schema = arrow::schema({
        arrow::field("timestamp",          arrow::uint64()),  ///< Global timestamp.
        arrow::field("id",                 arrow::int32()),   ///< Position ID.
        arrow::field("order_id",           arrow::int32()),   ///< Originating order ID.
        arrow::field("symbol",             arrow::utf8()),    ///< Trading symbol.
        arrow::field("instrument_type",    arrow::int32()),   ///< Dto::Trading::InstrumentType.
        arrow::field("quantity",           arrow::float64()), ///< Position size.
        arrow::field("entry_price",        arrow::float64()), ///< Entry price.
        arrow::field("is_long",            arrow::boolean()), ///< True if long.
        arrow::field("unrealized_pnl",     arrow::float64()), ///< Unrealized PnL.
        arrow::field("notional",           arrow::float64()), ///< Total notional.
        arrow::field("initial_margin",     arrow::float64()), ///< Initial margin.
        arrow::field("maintenance_margin", arrow::float64()), ///< Maintenance margin.
        arrow::field("fee",                arrow::float64()), ///< Accrued fees.
        arrow::field("leverage",           arrow::float64()), ///< Leverage.
        arrow::field("fee_rate",           arrow::float64())  ///< Fee rate.
        });

    /// @brief Serializer for Position payloads.
    /// @param src Pointer to QTrading::dto::Position.
    /// @param b   Builder to append all position fields.
    inline QTrading::Log::Serializer Serializer = [](const void* src, arrow::RecordBatchBuilder& b) {
        using P = QTrading::dto::Position;
        auto p = static_cast<const P*>(src);
        (void)b.GetFieldAs<arrow::Int32Builder>(1)->Append(p->id);
        (void)b.GetFieldAs<arrow::Int32Builder>(2)->Append(p->order_id);
        (void)b.GetFieldAs<arrow::StringBuilder>(3)->Append(p->symbol);
        (void)b.GetFieldAs<arrow::Int32Builder>(4)->Append(static_cast<int32_t>(p->instrument_type));
        (void)b.GetFieldAs<arrow::DoubleBuilder>(5)->Append(p->quantity);
        (void)b.GetFieldAs<arrow::DoubleBuilder>(6)->Append(p->entry_price);
        (void)b.GetFieldAs<arrow::BooleanBuilder>(7)->Append(p->is_long);
        (void)b.GetFieldAs<arrow::DoubleBuilder>(8)->Append(p->unrealized_pnl);
        (void)b.GetFieldAs<arrow::DoubleBuilder>(9)->Append(p->notional);
        (void)b.GetFieldAs<arrow::DoubleBuilder>(10)->Append(p->initial_margin);
        (void)b.GetFieldAs<arrow::DoubleBuilder>(11)->Append(p->maintenance_margin);
        (void)b.GetFieldAs<arrow::DoubleBuilder>(12)->Append(p->fee);
        (void)b.GetFieldAs<arrow::DoubleBuilder>(13)->Append(p->leverage);
        (void)b.GetFieldAs<arrow::DoubleBuilder>(14)->Append(p->fee_rate);
        };
}
