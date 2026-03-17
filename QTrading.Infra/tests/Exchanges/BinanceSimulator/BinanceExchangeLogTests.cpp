#include <fstream>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
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
};

} // namespace

TEST_F(BinanceExchangeLogTestFixture, BinanceExchangeLogUsesSinkLogger)
{
    WriteCsv("btc.csv", {
        { 0, 100.0, 101.0, 99.0, 100.5, 1000.0, 30000, 1000.0, 1, 0.0, 0.0 }
    });

    {
        BinanceExchange exchange({ { "BTCUSDT", (tmp_dir / "btc.csv").string() } }, logger, 1000.0);
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
