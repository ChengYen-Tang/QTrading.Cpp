#pragma once

#include <arrow/api.h>
#include <memory>
#include <string>

namespace QTrading::Log::FileLogger::FeatherV2 {

    enum class AccountEventType : int32_t {
        WalletDelta = 0,
        BalanceSnapshot = 1
    };

    struct AccountEventDto {
        uint64_t run_id{};
        uint64_t step_seq{};
        uint64_t event_seq{};

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
                arrow::field("available_balance_after", arrow::float64())
            });
        }

        inline void Serializer(const void* src, arrow::RecordBatchBuilder& builder)
        {
            const auto& e = *static_cast<const AccountEventDto*>(src);

            builder.GetFieldAs<arrow::UInt64Builder>(1)->Append(e.run_id);
            builder.GetFieldAs<arrow::UInt64Builder>(2)->Append(e.step_seq);
            builder.GetFieldAs<arrow::UInt64Builder>(3)->Append(e.event_seq);
            builder.GetFieldAs<arrow::UInt64Builder>(4)->Append(e.request_id);
            builder.GetFieldAs<arrow::Int64Builder>(5)->Append(e.source_order_id);
            builder.GetFieldAs<arrow::Int32Builder>(6)->Append(e.event_type);
            builder.GetFieldAs<arrow::DoubleBuilder>(7)->Append(e.wallet_delta);
            builder.GetFieldAs<arrow::DoubleBuilder>(8)->Append(e.wallet_balance_after);
            builder.GetFieldAs<arrow::DoubleBuilder>(9)->Append(e.margin_balance_after);
            builder.GetFieldAs<arrow::DoubleBuilder>(10)->Append(e.available_balance_after);
        }
    } // namespace AccountEvent

} // namespace QTrading::Log::FileLogger::FeatherV2
