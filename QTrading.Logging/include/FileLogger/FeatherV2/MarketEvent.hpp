#pragma once

#include <arrow/api.h>
#include <memory>
#include <string>

namespace QTrading::Log::FileLogger::FeatherV2 {

    struct MarketEventDto {
        uint64_t run_id{};
        uint64_t step_seq{};

        std::string symbol;
        bool has_kline{};

        double open{};
        double high{};
        double low{};
        double close{};
        double volume{};
        double taker_buy_base_volume{};
    };

    namespace MarketEvent {
        inline std::shared_ptr<arrow::Schema> Schema()
        {
            return arrow::schema({
                arrow::field("ts", arrow::uint64()),
                arrow::field("run_id", arrow::uint64()),
                arrow::field("step_seq", arrow::uint64()),
                arrow::field("symbol", arrow::utf8()),
                arrow::field("has_kline", arrow::boolean()),
                arrow::field("open", arrow::float64()),
                arrow::field("high", arrow::float64()),
                arrow::field("low", arrow::float64()),
                arrow::field("close", arrow::float64()),
                arrow::field("volume", arrow::float64()),
                arrow::field("taker_buy_base_volume", arrow::float64())
            });
        }

        inline void Serializer(const void* src, arrow::RecordBatchBuilder& builder)
        {
            const auto& e = *static_cast<const MarketEventDto*>(src);

            builder.GetFieldAs<arrow::UInt64Builder>(1)->Append(e.run_id);
            builder.GetFieldAs<arrow::UInt64Builder>(2)->Append(e.step_seq);
            builder.GetFieldAs<arrow::StringBuilder>(3)->Append(e.symbol);
            builder.GetFieldAs<arrow::BooleanBuilder>(4)->Append(e.has_kline);
            builder.GetFieldAs<arrow::DoubleBuilder>(5)->Append(e.open);
            builder.GetFieldAs<arrow::DoubleBuilder>(6)->Append(e.high);
            builder.GetFieldAs<arrow::DoubleBuilder>(7)->Append(e.low);
            builder.GetFieldAs<arrow::DoubleBuilder>(8)->Append(e.close);
            builder.GetFieldAs<arrow::DoubleBuilder>(9)->Append(e.volume);
            builder.GetFieldAs<arrow::DoubleBuilder>(10)->Append(e.taker_buy_base_volume);
        }
    } // namespace MarketEvent

} // namespace QTrading::Log::FileLogger::FeatherV2
