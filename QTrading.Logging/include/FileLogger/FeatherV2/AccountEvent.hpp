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

    struct AccountEventDto {
        uint64_t run_id{};
        uint64_t step_seq{};
        uint64_t event_seq{};
        uint64_t ts_local{};

        uint64_t request_id{};
        int64_t source_order_id{};

        int32_t event_type{};
        double wallet_delta{};

        double wallet_balance_after{};
        double margin_balance_after{};
        double available_balance_after{};
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
                arrow::field("event_type", arrow::int32()),
                arrow::field("wallet_delta", arrow::float64()),
                arrow::field("wallet_balance_after", arrow::float64()),
                arrow::field("margin_balance_after", arrow::float64()),
                arrow::field("available_balance_after", arrow::float64()),
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
            detail::AppendOrThrow(builder.GetFieldAs<arrow::Int32Builder>(6), e.event_type);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(7), e.wallet_delta);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(8), e.wallet_balance_after);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(9), e.margin_balance_after);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::DoubleBuilder>(10), e.available_balance_after);
            detail::AppendOrThrow(builder.GetFieldAs<arrow::UInt64Builder>(11), e.ts_local);
        }
    } // namespace AccountEvent

} // namespace QTrading::Log::FileLogger::FeatherV2
