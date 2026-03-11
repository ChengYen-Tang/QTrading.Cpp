#pragma once

#include "FileLogger/FeatherV2.hpp"
#include "Dto/Order.hpp"
#include "Dto/Trading/Side.hpp"

namespace QTrading::Log::FileLogger::FeatherV2::Order {
    /// @brief Arrow schema for order logs.
    inline auto Schema = arrow::schema({
        arrow::field("timestamp",           arrow::uint64()),  ///< Global timestamp.
        arrow::field("id",                  arrow::int32()),   ///< Order ID.
        arrow::field("symbol",              arrow::utf8()),    ///< Trading symbol.
        arrow::field("instrument_type",     arrow::int32()),   ///< Dto::Trading::InstrumentType.
        arrow::field("quantity",            arrow::float64()), ///< Order quantity.
        arrow::field("price",               arrow::float64()), ///< Order price.
        arrow::field("is_long",             arrow::boolean()), ///< True if long.
        arrow::field("reduce_only",         arrow::boolean()), ///< Reduce-only flag.
        arrow::field("closing_position_id", arrow::int32()),   ///< Position to close, or -1.
        arrow::field("close_position",      arrow::boolean()), ///< closePosition-style intent.
        arrow::field("quote_order_qty",     arrow::float64())  ///< Raw quoteOrderQty input when provided.
        });

    /// @brief Serializer for Order payloads.
    /// @param src Pointer to QTrading::dto::Order.
    /// @param b   Builder to append all order fields.
    inline QTrading::Log::Serializer Serializer = [](const void* src, arrow::RecordBatchBuilder& b) {
        using O = QTrading::dto::Order;
        auto o = static_cast<const O*>(src);
        (void)b.GetFieldAs<arrow::Int32Builder>(1)->Append(o->id);
        (void)b.GetFieldAs<arrow::StringBuilder>(2)->Append(o->symbol);
        (void)b.GetFieldAs<arrow::Int32Builder>(3)->Append(static_cast<int32_t>(o->instrument_type));
        (void)b.GetFieldAs<arrow::DoubleBuilder>(4)->Append(o->quantity);
        (void)b.GetFieldAs<arrow::DoubleBuilder>(5)->Append(o->price);
        (void)b.GetFieldAs<arrow::BooleanBuilder>(6)->Append(o->side == QTrading::Dto::Trading::OrderSide::Buy);
        (void)b.GetFieldAs<arrow::BooleanBuilder>(7)->Append(o->reduce_only);
        (void)b.GetFieldAs<arrow::Int32Builder>(8)->Append(o->closing_position_id);
        (void)b.GetFieldAs<arrow::BooleanBuilder>(9)->Append(o->close_position);
        (void)b.GetFieldAs<arrow::DoubleBuilder>(10)->Append(o->quote_order_qty);
        };
}
