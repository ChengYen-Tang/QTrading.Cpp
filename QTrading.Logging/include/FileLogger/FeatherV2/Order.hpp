#pragma once

#include "FileLogger/FeatherV2.hpp"
#include "Dto/Order.hpp"

namespace QTrading::Log::FileLogger::FeatherV2::Order {
    inline auto Schema = arrow::schema({
        arrow::field("timestamp",          arrow::uint64()),
        arrow::field("id",                 arrow::int32()),
        arrow::field("symbol",             arrow::utf8()),
        arrow::field("quantity",           arrow::float64()),
        arrow::field("price",              arrow::float64()),
        arrow::field("is_long",            arrow::boolean()),
        arrow::field("reduce_only",        arrow::boolean()),
        arrow::field("closing_position_id",arrow::int32())
    });

    /* 對應序列化函式 */
    inline QTrading::Log::Serializer Serializer = [](const void* src,
        arrow::RecordBatchBuilder& b) {
            using O = QTrading::dto::Order;
            auto o = static_cast<const O*>(src);

            b.GetFieldAs<arrow::Int32Builder>(1)->Append(o->id);
            b.GetFieldAs<arrow::StringBuilder>(2)->Append(o->symbol);
            b.GetFieldAs<arrow::DoubleBuilder>(3)->Append(o->quantity);
            b.GetFieldAs<arrow::DoubleBuilder>(4)->Append(o->price);
            b.GetFieldAs<arrow::BooleanBuilder>(5)->Append(o->is_long);
            b.GetFieldAs<arrow::BooleanBuilder>(6)->Append(o->reduce_only);
            b.GetFieldAs<arrow::Int32Builder>(7)->Append(o->closing_position_id);
    };
}
