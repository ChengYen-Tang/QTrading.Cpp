#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Support/BinanceExchangeSkeletonSupport.hpp"
#include "../../InfraLogTestFixture.hpp"

using namespace QTrading::Infra::Exchanges::BinanceSim;

namespace {

class BinanceExchangeLogTestFixture : public InfraLogTestFixture {
protected:
    void WriteCsv(
        const std::string& file_name,
        const std::vector<std::tuple<
            uint64_t, double, double, double, double, double,
            uint64_t, double, int, double, double>>& rows)
    {
        std::ofstream file(tmp_dir / file_name, std::ios::trunc);
        file << "openTime,open,high,low,close,volume,"
                "closeTime,quoteVol,tradeCnt,takerBB,takerBQ\n";
        for (const auto& row : rows) {
            file << std::get<0>(row) << ','
                 << std::get<1>(row) << ','
                 << std::get<2>(row) << ','
                 << std::get<3>(row) << ','
                 << std::get<4>(row) << ','
                 << std::get<5>(row) << ','
                 << std::get<6>(row) << ','
                 << std::get<7>(row) << ','
                 << std::get<8>(row) << ','
                 << std::get<9>(row) << ','
                 << std::get<10>(row) << '\n';
        }
    }

    void WriteFundingCsv(
        const std::string& file_name,
        const std::vector<std::tuple<uint64_t, double, std::optional<double>>>& rows)
    {
        std::ofstream file(tmp_dir / file_name, std::ios::trunc);
        file << "FundingTime,Rate,MarkPrice\n";
        for (const auto& row : rows) {
            file << std::get<0>(row) << ','
                 << std::get<1>(row) << ',';
            if (std::get<2>(row).has_value()) {
                file << *std::get<2>(row);
            }
            file << '\n';
        }
    }
};

} // namespace

TEST_F(BinanceExchangeLogTestFixture, BinanceExchangeLogUsesSinkLogger)
{
    WriteCsv("btc.csv", {
        { 0, 100.0, 101.0, 99.0, 100.5, 1000.0, 30000, 1000.0, 1, 0.0, 0.0 }
    });

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "btc.csv").string() } },
            logger,
            Support::BuildInitConfig(1000.0, 0));
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        const auto dto = market_channel->Receive();
        ASSERT_TRUE(dto.has_value());
        EXPECT_EQ(dto->get()->Timestamp, 0u);
    }
    StopLogger();

    ASSERT_FALSE(rows().empty());

    const auto account_module_id = ModuleId(QTrading::Log::LogModule::Account);
    ASSERT_NE(account_module_id, QTrading::Log::Logger::kInvalidModuleId);

    const QTrading::Log::Row* account_row = nullptr;
    for (const auto& row : rows()) {
        if (row.module_id == account_module_id) {
            account_row = &row;
            break;
        }
    }

    ASSERT_NE(account_row, nullptr);
    EXPECT_EQ(account_row->ts, 0u);
}

TEST_F(BinanceExchangeLogTestFixture, ChannelPayloadCanDirectlyValidateMarketLogRows)
{
    WriteCsv("btc.csv", {
        {      0, 100.0, 101.0,  99.0, 100.5, 1000.0, 30000, 1000.0, 1, 0.0, 0.0 },
        {  60000, 110.0, 111.0, 109.0, 110.5,  900.0, 90000,  900.0, 1, 0.0, 0.0 }
        });
    WriteCsv("eth.csv", {
        {  60000, 200.0, 202.0, 198.0, 201.0, 1200.0, 90000, 1200.0, 1, 0.0, 0.0 }
        });

    std::unordered_map<uint64_t, MultiKlinePtr> channel_by_ts;
    {
        BinanceExchange exchange(
            {
                { "BTCUSDT", (tmp_dir / "btc.csv").string() },
                { "ETHUSDT", (tmp_dir / "eth.csv").string() }
            },
            logger,
            Support::BuildInitConfig(1000.0, 0),
            99887766ull);

        auto market_channel = exchange.get_market_channel();
        while (exchange.step()) {
            const auto dto = market_channel->Receive();
            ASSERT_TRUE(dto.has_value());
            channel_by_ts[dto->get()->Timestamp] = dto.value();
        }
    }
    StopLogger();

    const auto market_module_id = ModuleId(QTrading::Log::LogModule::MarketEvent);
    ASSERT_NE(market_module_id, QTrading::Log::Logger::kInvalidModuleId);

    std::unordered_map<uint64_t, std::vector<const QTrading::Log::FileLogger::FeatherV2::MarketEventDto*>> rows_by_ts;
    for (const auto& row : rows()) {
        if (row.module_id != market_module_id) {
            continue;
        }
        const auto* payload = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::MarketEventDto>(&row);
        ASSERT_NE(payload, nullptr);
        EXPECT_GE(payload->ts_local, row.ts);
        rows_by_ts[row.ts].push_back(payload);
    }

    ASSERT_FALSE(rows_by_ts.empty());
    EXPECT_LE(rows_by_ts.size(), channel_by_ts.size());
    for (const auto& [ts, market_rows] : rows_by_ts) {
        auto dto_it = channel_by_ts.find(ts);
        ASSERT_NE(dto_it, channel_by_ts.end());
        const auto& dto = dto_it->second;
        ASSERT_TRUE(dto->symbols != nullptr);
        ASSERT_GE(market_rows.size(), dto->symbols->size());

        std::unordered_map<std::string, size_t> rows_per_symbol;
        for (const auto* payload : market_rows) {
            ++rows_per_symbol[payload->symbol];
        }
        for (const auto& symbol : *dto->symbols) {
            auto it = rows_per_symbol.find(symbol);
            ASSERT_NE(it, rows_per_symbol.end());
            EXPECT_GE(it->second, 1u);
        }

        std::unordered_map<std::string, std::vector<const QTrading::Log::FileLogger::FeatherV2::MarketEventDto*>> rows_grouped;
        for (const auto* payload : market_rows) {
            rows_grouped[payload->symbol].push_back(payload);
            EXPECT_EQ(payload->run_id, 99887766u);
            EXPECT_GE(payload->step_seq, 1u);
        }

        for (size_t symbol_index = 0; symbol_index < dto->symbols->size(); ++symbol_index) {
            const auto& symbol = dto->symbols->at(symbol_index);
            auto grouped_it = rows_grouped.find(symbol);
            ASSERT_NE(grouped_it, rows_grouped.end());

            const auto& maybe_kline = dto->trade_klines_by_id[symbol_index];
            bool found_matching_payload = false;
            for (const auto* payload : grouped_it->second) {
                if (payload->has_kline != maybe_kline.has_value()) {
                    continue;
                }
                if (!maybe_kline.has_value()) {
                    found_matching_payload = true;
                    break;
                }
                if (std::abs(payload->close - maybe_kline->ClosePrice) <= 1e-12
                    && std::abs(payload->volume - maybe_kline->Volume) <= 1e-12) {
                    found_matching_payload = true;
                    break;
                }
            }
            EXPECT_TRUE(found_matching_payload);
        }
    }
}

TEST_F(BinanceExchangeLogTestFixture, LogContractKeepsModuleOrderingTimestampAndExternalFieldNamingStable)
{
    WriteCsv("btc.csv", {
        {      0, 100.0, 101.0,  99.0, 100.0, 1000.0, 30000, 1000.0, 1, 0.0, 0.0 },
        {  60000, 110.0, 111.0, 109.0, 110.0, 1000.0, 90000, 1000.0, 1, 0.0, 0.0 }
        });
    WriteFundingCsv("btc_funding.csv", {
        { 60000, 0.001, 100.0 }
        });

    {
        BinanceExchange exchange(
            { BinanceExchange::SymbolDataset{
                "BTCUSDT",
                (tmp_dir / "btc.csv").string(),
                std::optional<std::string>((tmp_dir / "btc_funding.csv").string()) } },
            logger,
            Support::BuildInitConfig(1000.0, 0),
            55667788ull);

        using QTrading::Dto::Trading::OrderSide;
        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        auto market_channel = exchange.get_market_channel();
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }
    StopLogger();

    const auto account_module_id = ModuleId(QTrading::Log::LogModule::Account);
    const auto market_event_module_id = ModuleId(QTrading::Log::LogModule::MarketEvent);
    const auto account_event_module_id = ModuleId(QTrading::Log::LogModule::AccountEvent);
    const auto position_event_module_id = ModuleId(QTrading::Log::LogModule::PositionEvent);
    const auto order_event_module_id = ModuleId(QTrading::Log::LogModule::OrderEvent);

    size_t account_first = rows().size();
    size_t market_first = rows().size();
    size_t account_event_first = rows().size();
    size_t position_event_first = rows().size();
    size_t order_event_first = rows().size();

    uint64_t last_step_seq = 0;
    uint64_t last_event_seq = 0;
    uint64_t current_step = 0;
    for (size_t i = 0; i < rows().size(); ++i) {
        const auto& row = rows()[i];
        if (row.module_id == account_module_id) {
            account_first = std::min(account_first, i);
        }
        if (row.module_id == market_event_module_id) {
            market_first = std::min(market_first, i);
            const auto* payload = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::MarketEventDto>(&row);
            ASSERT_NE(payload, nullptr);
            EXPECT_EQ(payload->run_id, 55667788u);
            EXPECT_FALSE(payload->symbol.empty());
            EXPECT_TRUE(row.ts == 0u || row.ts == 60000u);

            if (current_step != payload->step_seq) {
                current_step = payload->step_seq;
                last_step_seq = payload->step_seq;
                last_event_seq = payload->event_seq;
            } else {
                EXPECT_GE(payload->event_seq, last_event_seq);
                last_event_seq = payload->event_seq;
            }
            EXPECT_GE(last_step_seq, 1u);
        }
        if (row.module_id == account_event_module_id) {
            account_event_first = std::min(account_event_first, i);
            const auto* payload = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::AccountEventDto>(&row);
            ASSERT_NE(payload, nullptr);
            EXPECT_EQ(payload->run_id, 55667788u);
            EXPECT_GE(payload->step_seq, 1u);
            EXPECT_GE(payload->event_seq, 1u);
            EXPECT_GE(payload->ts_local, row.ts);
        }
        if (row.module_id == position_event_module_id) {
            position_event_first = std::min(position_event_first, i);
            const auto* payload = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::PositionEventDto>(&row);
            ASSERT_NE(payload, nullptr);
            EXPECT_EQ(payload->run_id, 55667788u);
            EXPECT_GE(payload->step_seq, 1u);
            EXPECT_GE(payload->event_seq, 1u);
            EXPECT_GE(payload->ts_local, row.ts);
        }
        if (row.module_id == order_event_module_id) {
            order_event_first = std::min(order_event_first, i);
            const auto* payload = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::OrderEventDto>(&row);
            ASSERT_NE(payload, nullptr);
            EXPECT_EQ(payload->run_id, 55667788u);
            EXPECT_GE(payload->step_seq, 1u);
            EXPECT_GE(payload->event_seq, 1u);
            EXPECT_GE(payload->ts_local, row.ts);
        }

        const auto snapshot = BuildLegacyLogContractSnapshot(&row);
        if (snapshot.has_value()) {
            EXPECT_FALSE(snapshot->module_name.empty());
            if (snapshot->module_name != QTrading::Log::LogModuleToString(QTrading::Log::LogModule::RunMetadata)
                && snapshot->run_id != 0u) {
                EXPECT_EQ(snapshot->run_id, 55667788u);
            }
        }
    }

    ASSERT_LT(account_first, rows().size());
    ASSERT_LT(market_first, rows().size());
    ASSERT_LT(account_event_first, rows().size());
    ASSERT_LT(position_event_first, rows().size());
    ASSERT_LT(order_event_first, rows().size());

    EXPECT_LT(account_first, market_first);
    EXPECT_LT(market_first, account_event_first);
    EXPECT_LT(account_event_first, position_event_first);
    EXPECT_LT(position_event_first, order_event_first);
}
