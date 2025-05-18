#pragma once

#include "FileLogger/FeatherV2.hpp"
#include "Dto/AccountLog.hpp"

namespace QTrading::Log::FileLogger::FeatherV2::AccountLog {
    inline auto Schema = arrow::schema({
        arrow::field("timestamp", arrow::uint64()),
        arrow::field("balance",   arrow::float64()),
        arrow::field("unreal_pnl",arrow::float64()),
        arrow::field("equity",    arrow::float64())
    });

    /* 對應序列化函式 */
    inline QTrading::Log::Serializer Serializer = [](const void* src,
        arrow::RecordBatchBuilder& b) {
            using A = QTrading::dto::AccountLog;     // 你的自訂 AccountLog 結構
            auto a = static_cast<const A*>(src);

            b.GetFieldAs<arrow::DoubleBuilder>(1)->Append(a->balance);
            b.GetFieldAs<arrow::DoubleBuilder>(2)->Append(a->unreal_pnl);
            b.GetFieldAs<arrow::DoubleBuilder>(3)->Append(a->equity);
    };
}
