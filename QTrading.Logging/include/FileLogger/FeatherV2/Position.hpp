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
        b.GetFieldAs<arrow::Int32Builder>(1)->Append(p->id);
        b.GetFieldAs<arrow::Int32Builder>(2)->Append(p->order_id);
        b.GetFieldAs<arrow::StringBuilder>(3)->Append(p->symbol);
        b.GetFieldAs<arrow::DoubleBuilder>(4)->Append(p->quantity);
        b.GetFieldAs<arrow::DoubleBuilder>(5)->Append(p->entry_price);
        b.GetFieldAs<arrow::BooleanBuilder>(6)->Append(p->is_long);
        b.GetFieldAs<arrow::DoubleBuilder>(7)->Append(p->unrealized_pnl);
        b.GetFieldAs<arrow::DoubleBuilder>(8)->Append(p->notional);
        b.GetFieldAs<arrow::DoubleBuilder>(9)->Append(p->initial_margin);
        b.GetFieldAs<arrow::DoubleBuilder>(10)->Append(p->maintenance_margin);
        b.GetFieldAs<arrow::DoubleBuilder>(11)->Append(p->fee);
        b.GetFieldAs<arrow::DoubleBuilder>(12)->Append(p->leverage);
        b.GetFieldAs<arrow::DoubleBuilder>(13)->Append(p->fee_rate);
        };
}
