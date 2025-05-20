#pragma once

#include "FileLogger/FeatherV2.hpp"
#include "Dto/AccountLog.hpp"

namespace QTrading::Log::FileLogger::FeatherV2::AccountLog {
    /// @brief Arrow schema for account logs: ts, balance, unrealized PnL, equity.
    inline auto Schema = arrow::schema({
        arrow::field("timestamp", arrow::uint64()),    ///< Global timestamp.
        arrow::field("balance",   arrow::float64()),   ///< Account balance.
        arrow::field("unreal_pnl",arrow::float64()),   ///< Unrealized profit & loss.
        arrow::field("equity",    arrow::float64())    ///< Total equity.
        });

    /// @brief Serializer for AccountLog payloads.
    /// @param src Pointer to QTrading::dto::AccountLog.
    /// @param b   Builder to append balance, PnL, equity.
    inline QTrading::Log::Serializer Serializer = [](const void* src, arrow::RecordBatchBuilder& b) {
        using A = QTrading::dto::AccountLog;
        auto a = static_cast<const A*>(src);
        b.GetFieldAs<arrow::DoubleBuilder>(1)->Append(a->balance);
        b.GetFieldAs<arrow::DoubleBuilder>(2)->Append(a->unreal_pnl);
        b.GetFieldAs<arrow::DoubleBuilder>(3)->Append(a->equity);
        };
}
