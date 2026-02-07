#pragma once

#include <arrow/api.h>
#include <memory>
#include <string>

#include "FileLogger/FeatherV2/ArrowAppend.hpp"

namespace QTrading::Log::FileLogger::FeatherV2 {

    enum class AccountEventType : int32_t {
        WalletDelta = 0,
        BalanceSnapshot = 1
    };

    enum class AccountLedger : int32_t {
        Unknown = -1,
        Perp = 0,
        Spot = 1,
        Both = 2
    };

    struct AccountEventDto {
        uint64_t run_id{};
        uint64_t step_seq{};
        uint64_t event_seq{};
        uint64_t ts_local{};

        uint64_t request_id{};
        int64_t source_order_id{};
        std::string symbol;
        int32_t instrument_type{ -1 }; // Dto::Trading::InstrumentType, -1 means N/A.
        int32_t ledger{ static_cast<int32_t>(AccountLedger::Unknown) };

        int32_t event_type{};
        double wallet_delta{};

        // Legacy perp-oriented snapshots.
        double wallet_balance_after{};
        double margin_balance_after{};
        double available_balance_after{};

        // Dual-ledger snapshots.
        double perp_wallet_balance_after{};
        double perp_margin_balance_after{};
        double perp_available_balance_after{};
        double spot_wallet_balance_after{};
        double spot_available_balance_after{};
        double spot_inventory_value_after{};
        double spot_ledger_value_after{};
        double total_cash_balance_after{};
        double total_ledger_value_after{};
    };

    namespace AccountEvent {
        inline std::shared_ptr<arrow::Schema> Schema()
        {
            return arrow::schema({
                arrow::field("ts", arrow::uint64()),
                arrow::field("run_id", arrow::uint64()),
                arrow::field("step_seq", arrow::uint64()),
                arrow::field("event_seq", arrow::uint64()),
                arrow::field("request_id", arrow::uint64()),
                arrow::field("source_order_id", arrow::int64()),
                arrow::field("symbol", arrow::utf8()),
                arrow::field("instrument_type", arrow::int32()),
                arrow::field("ledger", arrow::int32()),
                arrow::field("event_type", arrow::int32()),
                arrow::field("wallet_delta", arrow::float64()),
                arrow::field("wallet_balance_after", arrow::float64()),
                arrow::field("margin_balance_after", arrow::float64()),
                arrow::field("available_balance_after", arrow::float64()),
                arrow::field("perp_wallet_balance_after", arrow::float64()),
                arrow::field("perp_margin_balance_after", arrow::float64()),
                arrow::field("perp_available_balance_after", arrow::float64()),
                arrow::field("spot_wallet_balance_after", arrow::float64()),
                arrow::field("spot_available_balance_after", arrow::float64()),
                arrow::field("spot_inventory_value_after", arrow::float64()),
                arrow::field("spot_ledger_value_after", arrow::float64()),
                arrow::field("total_cash_balance_after", arrow::float64()),
                arrow::field("total_ledger_value_after", arrow::float64()),
                arrow::field("ts_local", arrow::uint64())
            });
        }

        inline void Serializer(const void* src, arrow::RecordBatchBuilder& builder)
        {
            const auto& e = *static_cast<const AccountEventDto*>(src);

            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(1), e.run_id);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(2), e.step_seq);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(3), e.event_seq);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(4), e.request_id);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int64Builder>(5), e.source_order_id);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::StringBuilder>(6), e.symbol);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int32Builder>(7), e.instrument_type);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int32Builder>(8), e.ledger);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int32Builder>(9), e.event_type);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(10), e.wallet_delta);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(11), e.wallet_balance_after);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(12), e.margin_balance_after);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(13), e.available_balance_after);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(14), e.perp_wallet_balance_after);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(15), e.perp_margin_balance_after);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(16), e.perp_available_balance_after);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(17), e.spot_wallet_balance_after);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(18), e.spot_available_balance_after);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(19), e.spot_inventory_value_after);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(20), e.spot_ledger_value_after);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(21), e.total_cash_balance_after);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(22), e.total_ledger_value_after);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(23), e.ts_local);
        }
    } // namespace AccountEvent

} // namespace QTrading::Log::FileLogger::FeatherV2
