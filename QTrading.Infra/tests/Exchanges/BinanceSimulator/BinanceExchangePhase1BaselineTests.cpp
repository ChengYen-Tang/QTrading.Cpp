#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <tuple>
#include <vector>

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Support/BinanceExchangeSkeletonSupport.hpp"

namespace {

using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
namespace fs = std::filesystem;

class BinanceExchangeFixture : public ::testing::Test {
protected:
    fs::path tmp_dir;

    void SetUp() override
    {
        tmp_dir = fs::temp_directory_path() /
            (std::string("QTradingPhase1_") +
             ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::create_directories(tmp_dir);
    }

    void TearDown() override
    {
        fs::remove_all(tmp_dir);
    }

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

    BinanceExchange MakeExchange(const std::vector<BinanceExchange::SymbolDataset>& datasets)
    {
        return BinanceExchange(
            datasets,
            nullptr,
            QTrading::Infra::Exchanges::BinanceSim::Support::BuildInitConfig(1000.0, 0));
    }
};

// Baseline reference: BinanceExchangeFixture.SymbolsSynchronisedWithHoles
TEST_F(BinanceExchangeFixture, SymbolsSynchronisedWithHoles)
{
    WriteCsv("btc.csv", {
        {      0, 1,1,1,1,100, 30000,100,1,0,0 },
        {  60000, 2,2,2,2,200, 90000,200,1,0,0 }
    });
    WriteCsv("eth.csv", {
        {  30000,10,10,10,10, 50, 60000, 50,1,0,0 },
        {  60000,20,20,20,20, 70, 90000, 70,1,0,0 }
    });

    BinanceExchange exchange = MakeExchange({
        { "BTCUSDT", (tmp_dir / "btc.csv").string() },
        { "ETHUSDT", (tmp_dir / "eth.csv").string() },
    });

    auto market_channel = exchange.get_market_channel();

    ASSERT_TRUE(exchange.step());
    auto dto1 = market_channel->Receive();
    ASSERT_TRUE(dto1.has_value());
    EXPECT_EQ(dto1->get()->Timestamp, 0u);
    ASSERT_TRUE(dto1->get()->symbols);
    ASSERT_EQ(dto1->get()->symbols->size(), 2u);
    const auto symbols = dto1->get()->symbols;
    auto find_id = [&](const std::string& symbol) -> std::size_t {
        for (std::size_t i = 0; i < symbols->size(); ++i) {
            if ((*symbols)[i] == symbol) {
                return i;
            }
        }
        return symbols->size();
    };
    const auto btc_id = find_id("BTCUSDT");
    const auto eth_id = find_id("ETHUSDT");
    ASSERT_LT(btc_id, symbols->size());
    ASSERT_LT(eth_id, symbols->size());
    EXPECT_TRUE(dto1->get()->trade_klines_by_id[btc_id].has_value());
    EXPECT_FALSE(dto1->get()->trade_klines_by_id[eth_id].has_value());

    ASSERT_TRUE(exchange.step());
    auto dto2 = market_channel->Receive();
    ASSERT_TRUE(dto2.has_value());
    EXPECT_EQ(dto2->get()->Timestamp, 30000u);

    ASSERT_TRUE(exchange.step());
    auto dto3 = market_channel->Receive();
    ASSERT_TRUE(dto3.has_value());
    EXPECT_EQ(dto3->get()->Timestamp, 60000u);

    EXPECT_FALSE(exchange.step());
}

TEST_F(BinanceExchangeFixture, StepSuccessPublishesMarketChannel)
{
    WriteCsv("btc.csv", {
        { 1000, 100,100,100,100,1000, 61000,100,1,0,0 }
    });

    BinanceExchange exchange = MakeExchange({
        { "BTCUSDT", (tmp_dir / "btc.csv").string() },
    });

    ASSERT_TRUE(exchange.step());
    auto dto = exchange.get_market_channel()->TryReceive();
    ASSERT_TRUE(dto.has_value());
    ASSERT_TRUE(dto.value() != nullptr);
    EXPECT_EQ(dto.value()->Timestamp, 1000u);
}

// Baseline reference: EOF behavior from BinanceExchangeFixture.PushOnlyOnChange
TEST_F(BinanceExchangeFixture, ReplayExhaustedClosesAllPublicChannels)
{
    WriteCsv("btc.csv", {
        { 1000, 100,100,100,100,1000, 61000,100,1,0,0 }
    });

    BinanceExchange exchange = MakeExchange({
        { "BTCUSDT", (tmp_dir / "btc.csv").string() },
    });

    ASSERT_TRUE(exchange.step());
    EXPECT_FALSE(exchange.get_market_channel()->IsClosed());
    EXPECT_FALSE(exchange.get_position_channel()->IsClosed());
    EXPECT_FALSE(exchange.get_order_channel()->IsClosed());

    EXPECT_FALSE(exchange.step());
    EXPECT_TRUE(exchange.get_market_channel()->IsClosed());
    EXPECT_TRUE(exchange.get_position_channel()->IsClosed());
    EXPECT_TRUE(exchange.get_order_channel()->IsClosed());
}

// Baseline observability alignment: no additional channel payload after exhaustion.
TEST_F(BinanceExchangeFixture, ReplayExhaustedKeepsChannelsClosedOnSubsequentSteps)
{
    WriteCsv("btc.csv", {
        { 1000, 100,100,100,100,1000, 61000,100,1,0,0 }
    });

    BinanceExchange exchange = MakeExchange({
        { "BTCUSDT", (tmp_dir / "btc.csv").string() },
    });

    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();

    ASSERT_FALSE(exchange.step());
    ASSERT_TRUE(exchange.get_market_channel()->IsClosed());
    ASSERT_TRUE(exchange.get_position_channel()->IsClosed());
    ASSERT_TRUE(exchange.get_order_channel()->IsClosed());

    EXPECT_FALSE(exchange.step());
    EXPECT_FALSE(exchange.get_market_channel()->TryReceive().has_value());
    EXPECT_FALSE(exchange.get_position_channel()->TryReceive().has_value());
    EXPECT_FALSE(exchange.get_order_channel()->TryReceive().has_value());
}

// Baseline reference: BinanceExchangeFixture.StatusSnapshotPriceIncludesTradeMarkAndIndex
TEST_F(BinanceExchangeFixture, StatusSnapshotCarriesLatestTradePriceContext)
{
    WriteCsv("btc.csv", {
        { 1000, 100,101,99,123.5,1000, 61000,100,1,0,0 }
    });

    BinanceExchange exchange = MakeExchange({
        { "BTCUSDT", (tmp_dir / "btc.csv").string() },
    });

    ASSERT_TRUE(exchange.step());
    BinanceExchange::StatusSnapshot snapshot{};
    exchange.FillStatusSnapshot(snapshot);

    ASSERT_EQ(snapshot.prices.size(), 1u);
    EXPECT_EQ(snapshot.prices[0].symbol, "BTCUSDT");
    EXPECT_TRUE(snapshot.prices[0].has_trade_price);
    EXPECT_TRUE(snapshot.prices[0].has_price);
    EXPECT_DOUBLE_EQ(snapshot.prices[0].trade_price, 123.5);
    EXPECT_DOUBLE_EQ(snapshot.prices[0].price, 123.5);
    EXPECT_FALSE(snapshot.prices[0].has_mark_price);
    EXPECT_FALSE(snapshot.prices[0].has_index_price);
}

// Baseline reference: BinanceExchangeFixture.StatusSnapshotExposesDualLedgerTotals
TEST_F(BinanceExchangeFixture, StatusSnapshotExposesMinimalDualLedgerBalances)
{
    WriteCsv("btc.csv", {
        { 1000, 100,100,100,100,1000, 61000,100,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 600.0;
    init.perp_initial_wallet = 400.0;
    BinanceExchange exchange(
        { { "BTCUSDT", (tmp_dir / "btc.csv").string() } },
        nullptr,
        init);

    BinanceExchange::StatusSnapshot snapshot{};
    exchange.FillStatusSnapshot(snapshot);
    EXPECT_DOUBLE_EQ(snapshot.spot_cash_balance, 600.0);
    EXPECT_DOUBLE_EQ(snapshot.perp_wallet_balance, 400.0);
    EXPECT_DOUBLE_EQ(snapshot.total_cash_balance, 1000.0);
    EXPECT_DOUBLE_EQ(snapshot.total_ledger_value, 1000.0);
}

TEST_F(BinanceExchangeFixture, StatusSnapshotTracksReplayProgressAndTimestamp)
{
    WriteCsv("btc.csv", {
        { 1000, 100,100,100,100,1000, 61000,100,1,0,0 },
        { 2000, 101,101,101,101,1000, 62000,100,1,0,0 }
    });

    BinanceExchange exchange = MakeExchange({
        { "BTCUSDT", (tmp_dir / "btc.csv").string() },
    });

    BinanceExchange::StatusSnapshot snapshot{};
    exchange.FillStatusSnapshot(snapshot);
    EXPECT_EQ(snapshot.ts_exchange, 0u);
    EXPECT_NEAR(snapshot.progress_pct, 0.0, 1e-12);

    ASSERT_TRUE(exchange.step());
    exchange.FillStatusSnapshot(snapshot);
    EXPECT_EQ(snapshot.ts_exchange, 1000u);
    EXPECT_NEAR(snapshot.progress_pct, 50.0, 1e-12);

    ASSERT_TRUE(exchange.step());
    exchange.FillStatusSnapshot(snapshot);
    EXPECT_EQ(snapshot.ts_exchange, 2000u);
    EXPECT_NEAR(snapshot.progress_pct, 100.0, 1e-12);
}

} // namespace
