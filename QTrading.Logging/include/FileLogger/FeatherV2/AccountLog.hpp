#pragma once

#include "FileLogger/FeatherV2.hpp"
#include "Dto/AccountLog.hpp"

namespace QTrading::Log::FileLogger::FeatherV2::AccountLog {
    /// @brief Arrow schema for account logs with dual-ledger extensions.
    inline auto Schema = arrow::schema({
        arrow::field("timestamp", arrow::uint64()),    ///< Global timestamp.
        arrow::field("balance",   arrow::float64()),   ///< Legacy perp wallet balance.
        arrow::field("unreal_pnl",arrow::float64()),   ///< Legacy perp unrealized PnL.
        arrow::field("equity",    arrow::float64()),   ///< Legacy perp equity.
        arrow::field("perp_wallet_balance", arrow::float64()),
        arrow::field("perp_available_balance", arrow::float64()),
        arrow::field("perp_ledger_value", arrow::float64()),
        arrow::field("spot_cash_balance", arrow::float64()),
        arrow::field("spot_available_balance", arrow::float64()),
        arrow::field("spot_inventory_value", arrow::float64()),
        arrow::field("spot_ledger_value", arrow::float64()),
        arrow::field("total_cash_balance", arrow::float64()),
        arrow::field("total_ledger_value", arrow::float64())
        });

    /// @brief Serializer for AccountLog payloads.
    /// @param src Pointer to QTrading::dto::AccountLog.
    /// @param b   Builder to append dual-ledger account fields.
    inline QTrading::Log::Serializer Serializer = [](const void* src, arrow::RecordBatchBuilder& b) {
        using A = QTrading::dto::AccountLog;
        auto a = static_cast<const A*>(src);
        (void)b.GetFieldAs<arrow::DoubleBuilder>(1)->Append(a->balance);
        (void)b.GetFieldAs<arrow::DoubleBuilder>(2)->Append(a->unreal_pnl);
        (void)b.GetFieldAs<arrow::DoubleBuilder>(3)->Append(a->equity);
        (void)b.GetFieldAs<arrow::DoubleBuilder>(4)->Append(a->perp_wallet_balance);
        (void)b.GetFieldAs<arrow::DoubleBuilder>(5)->Append(a->perp_available_balance);
        (void)b.GetFieldAs<arrow::DoubleBuilder>(6)->Append(a->perp_ledger_value);
        (void)b.GetFieldAs<arrow::DoubleBuilder>(7)->Append(a->spot_cash_balance);
        (void)b.GetFieldAs<arrow::DoubleBuilder>(8)->Append(a->spot_available_balance);
        (void)b.GetFieldAs<arrow::DoubleBuilder>(9)->Append(a->spot_inventory_value);
        (void)b.GetFieldAs<arrow::DoubleBuilder>(10)->Append(a->spot_ledger_value);
        (void)b.GetFieldAs<arrow::DoubleBuilder>(11)->Append(a->total_cash_balance);
        (void)b.GetFieldAs<arrow::DoubleBuilder>(12)->Append(a->total_ledger_value);
        };
}
