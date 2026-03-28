#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "InfraLogTestFixture.hpp"
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
            (std::string("QTradingReplay_") +
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

    void WriteFundingCsv(
        const std::string& file_name,
        const std::vector<std::tuple<uint64_t, double, std::optional<double>>>& rows)
    {
        std::ofstream file(tmp_dir / file_name, std::ios::trunc);
        file << "FundingTime,Rate,MarkPrice\n";
        for (const auto& row : rows) {
            file << std::get<0>(row) << ',' << std::get<1>(row) << ',';
            if (std::get<2>(row).has_value()) {
                file << *std::get<2>(row);
            }
            file << '\n';
        }
    }

    void WriteCompactCsv(
        const std::string& file_name,
        const std::vector<std::tuple<uint64_t, double, double, double, double, uint64_t>>& rows)
    {
        std::ofstream file(tmp_dir / file_name, std::ios::trunc);
        file << "openTime,open,high,low,close,closeTime\n";
        for (const auto& row : rows) {
            file << std::get<0>(row) << ','
                 << std::get<1>(row) << ','
                 << std::get<2>(row) << ','
                 << std::get<3>(row) << ','
                 << std::get<4>(row) << ','
                 << std::get<5>(row) << '\n';
        }
    }

    BinanceExchange MakeExchange(const std::vector<BinanceExchange::SymbolDataset>& datasets)
    {
        return BinanceExchange(
            datasets,
            nullptr,
            QTrading::Infra::Exchanges::BinanceSim::Support::BuildInitConfig(1000.0, 0));
    }

    BinanceExchange MakeExchangeWithInit(
        const std::vector<BinanceExchange::SymbolDataset>& datasets,
        const Account::AccountInitConfig& init)
    {
        return BinanceExchange(datasets, nullptr, init);
    }
};

class BinanceExchangeLogFixture : public InfraLogTestFixture {
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
            file << std::get<0>(row) << ',' << std::get<1>(row) << ',';
            if (std::get<2>(row).has_value()) {
                file << *std::get<2>(row);
            }
            file << '\n';
        }
    }
};

// Source reference: BinanceExchangeFixture.SymbolsSynchronisedWithHoles
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

// Source reference: EOF behavior from BinanceExchangeFixture.PushOnlyOnChange
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

// Observability alignment: no additional channel payload after exhaustion.
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

// Source reference: BinanceExchangeFixture.StatusSnapshotPriceIncludesTradeMarkAndIndex
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

// Source reference: BinanceExchangeFixture.StatusSnapshotExposesDualLedgerTotals
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

TEST_F(BinanceExchangeFixture, SpotSellWithoutInventoryRejectsSynchronously)
{
    WriteCsv("btc.csv", {
        {      0,100,100,100,100,100, 30000,100,1,0,0 },
        {  60000,100,100,100,100,100, 90000,100,1,0,0 }
    });

    BinanceExchange exchange = MakeExchange({
        { "BTCUSDT", (tmp_dir / "btc.csv").string() },
    });
    ASSERT_FALSE(exchange.spot.place_order(
        "BTCUSDT",
        1.0,
        100.0,
        QTrading::Dto::Trading::OrderSide::Sell));
    EXPECT_TRUE(exchange.get_all_open_orders().empty());
}

// Source reference: BinanceExchangeFixture.DomainFacadeRoutesByInstrumentType
TEST_F(BinanceExchangeFixture, SpotAndPerpSyncOrdersAreStoredByInstrumentType)
{
    WriteCsv("btc.csv", {
        { 1000, 100,100,100,100,1000, 61000,100,1,0,0 }
    });
    WriteCsv("eth.csv", {
        { 1000, 200,200,200,200,1000, 61000,100,1,0,0 }
    });

    BinanceExchange exchange = MakeExchange({
        { "BTCUSDT", (tmp_dir / "btc.csv").string() },
        { "ETHUSDT", (tmp_dir / "eth.csv").string() },
    });

    ASSERT_TRUE(exchange.spot.place_order("BTCUSDT", 1.0, 100.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.perp.place_order("ETHUSDT", 2.0, 200.0, QTrading::Dto::Trading::OrderSide::Sell));

    const auto& orders = exchange.get_all_open_orders();
    ASSERT_EQ(orders.size(), 2u);
    EXPECT_EQ(orders[0].instrument_type, QTrading::Dto::Trading::InstrumentType::Spot);
    EXPECT_EQ(orders[1].instrument_type, QTrading::Dto::Trading::InstrumentType::Perp);
}

TEST_F(BinanceExchangeFixture, PerpCancelOpenOrdersRemovesPerpOrdersOnly)
{
    WriteCsv("btc.csv", {
        { 1000, 100,100,100,100,1000, 61000,100,1,0,0 }
    });
    WriteCsv("eth.csv", {
        { 1000, 200,200,200,200,1000, 61000,100,1,0,0 }
    });

    BinanceExchange exchange = MakeExchange({
        { "BTCUSDT", (tmp_dir / "btc.csv").string() },
        { "ETHUSDT", (tmp_dir / "eth.csv").string() },
    });

    ASSERT_TRUE(exchange.spot.place_order("BTCUSDT", 1.0, 100.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 2.0, 100.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.perp.place_order("ETHUSDT", 2.0, 200.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_EQ(exchange.get_all_open_orders().size(), 3u);

    exchange.perp.cancel_open_orders("BTCUSDT");
    const auto& orders = exchange.get_all_open_orders();
    ASSERT_EQ(orders.size(), 2u);
    EXPECT_EQ(orders[0].instrument_type, QTrading::Dto::Trading::InstrumentType::Spot);
    EXPECT_EQ(orders[0].symbol, "BTCUSDT");
    EXPECT_EQ(orders[1].symbol, "ETHUSDT");
}

TEST_F(BinanceExchangeFixture, ExplicitInstrumentTypeAppliesWithoutSuffixNaming)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000, 100,100,100,100,1000, 90000,100,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 1000.0;
    init.perp_initial_wallet = 0.0;
    BinanceExchange exchange = MakeExchangeWithInit({
        { "BTCUSDT",
            (tmp_dir / "btc.csv").string(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            QTrading::Dto::Trading::InstrumentType::Spot }
    }, init);

    EXPECT_FALSE(exchange.spot.place_order("BTCUSDT", 1.0, 100.0, QTrading::Dto::Trading::OrderSide::Sell));
    ASSERT_TRUE(exchange.spot.place_order("BTCUSDT", 1.0, 100.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();
    ASSERT_EQ(exchange.get_all_positions().size(), 1u);
    EXPECT_EQ(exchange.get_all_positions()[0].instrument_type, QTrading::Dto::Trading::InstrumentType::Spot);
    ASSERT_TRUE(exchange.spot.place_order("BTCUSDT", 0.5, 100.0, QTrading::Dto::Trading::OrderSide::Sell));
}

TEST_F(BinanceExchangeFixture, CompatibilityModeUnspecifiedInstrumentDefaultsToPerpPolicy)
{
    WriteCsv("btc.csv", {
        { 0, 100,100,100,100,1000, 30000,100,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange = MakeExchangeWithInit({
        { "BTCUSDT", (tmp_dir / "btc.csv").string() }
    }, init);

    exchange.set_symbol_leverage("BTCUSDT", 10.0);
    EXPECT_DOUBLE_EQ(exchange.get_symbol_leverage("BTCUSDT"), 10.0);
    EXPECT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, 100.0, QTrading::Dto::Trading::OrderSide::Buy));
}

TEST_F(BinanceExchangeFixture, StrictModeRejectsUnknownSymbolOrderPlacement)
{
    WriteCsv("btc.csv", {
        { 0, 100,100,100,100,1000, 30000,100,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange = MakeExchangeWithInit({
        { "BTCUSDT", (tmp_dir / "btc.csv").string() }
    }, init);

    EXPECT_FALSE(exchange.perp.place_order("UNKNOWNUSDT", 1.0, 100.0, QTrading::Dto::Trading::OrderSide::Buy));
}

TEST_F(BinanceExchangeFixture, StrictModeDomainApiRejectsUnknownSymbolOrderPlacement)
{
    WriteCsv("btc.csv", {
        { 0, 100,100,100,100,1000, 30000,100,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 1000.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange = MakeExchangeWithInit({
        { "BTCUSDT", (tmp_dir / "btc.csv").string() }
    }, init);

    EXPECT_FALSE(exchange.perp.place_order("UNKNOWNUSDT", 1.0, 100.0, QTrading::Dto::Trading::OrderSide::Buy));
    EXPECT_FALSE(exchange.spot.place_order("UNKNOWNUSDT", 1.0, 100.0, QTrading::Dto::Trading::OrderSide::Buy));
}

TEST_F(BinanceExchangeFixture, SetAndGetSymbolLeverage)
{
    WriteCsv("btc.csv", {
        { 0, 100,100,100,100,1000, 30000,100,1,0,0 }
    });
    BinanceExchange exchange = MakeExchange({
        { "BTCUSDT", (tmp_dir / "btc.csv").string() }
    });

    EXPECT_DOUBLE_EQ(exchange.get_symbol_leverage("BTCUSDT"), 1.0);
    exchange.set_symbol_leverage("BTCUSDT", 15.0);
    EXPECT_DOUBLE_EQ(exchange.get_symbol_leverage("BTCUSDT"), 15.0);
    EXPECT_DOUBLE_EQ(exchange.perp.get_symbol_leverage("BTCUSDT"), 15.0);
}

TEST_F(BinanceExchangeFixture, SpotSymbolLeverageIsAlwaysOne)
{
    WriteCsv("btc.csv", {
        { 0, 100,100,100,100,1000, 30000,100,1,0,0 }
    });
    Account::AccountInitConfig init{};
    init.spot_initial_cash = 1000.0;
    init.perp_initial_wallet = 0.0;
    BinanceExchange exchange = MakeExchangeWithInit({
        { "BTCUSDT",
            (tmp_dir / "btc.csv").string(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            QTrading::Dto::Trading::InstrumentType::Spot }
    }, init);

    exchange.set_symbol_leverage("BTCUSDT", 20.0);
    EXPECT_DOUBLE_EQ(exchange.get_symbol_leverage("BTCUSDT"), 1.0);
}

TEST_F(BinanceExchangeFixture, PerpClosePositionOrderRejectsWithoutReduciblePosition)
{
    WriteCsv("btc.csv", {
        { 1000, 100,100,100,100,1000, 61000,100,1,0,0 }
    });

    BinanceExchange exchange = MakeExchange({
        { "BTCUSDT", (tmp_dir / "btc.csv").string() },
    });

    ASSERT_FALSE(exchange.perp.place_close_position_order(
        "BTCUSDT",
        QTrading::Dto::Trading::OrderSide::Sell,
        QTrading::Dto::Trading::PositionSide::Long,
        0.0,
        "cid-close"));
    EXPECT_TRUE(exchange.get_all_open_orders().empty());
}

TEST_F(BinanceExchangeFixture, PerpClosePositionOrderClosesExistingExposure)
{
    WriteCsv("btc.csv", {
        {      0,100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000,100,100,100,100,1000, 90000,100,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT", (tmp_dir / "btc.csv").string() } },
        nullptr,
        init);

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();
    ASSERT_EQ(exchange.get_all_positions().size(), 1u);

    ASSERT_TRUE(exchange.perp.place_close_position_order(
        "BTCUSDT",
        QTrading::Dto::Trading::OrderSide::Sell,
        QTrading::Dto::Trading::PositionSide::Both,
        0.0,
        "cid-close"));
    ASSERT_EQ(exchange.get_all_open_orders().size(), 1u);
    EXPECT_TRUE(exchange.get_all_open_orders()[0].close_position);
    EXPECT_TRUE(exchange.get_all_open_orders()[0].reduce_only);

    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();
    EXPECT_TRUE(exchange.get_all_open_orders().empty());
    EXPECT_TRUE(exchange.get_all_positions().empty());
}

TEST_F(BinanceExchangeFixture, PerpReduceOnlyOrderDoesNotFlipExposure)
{
    WriteCsv("btc.csv", {
        {      0,100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000,100,100,100,100,1000, 90000,100,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT", (tmp_dir / "btc.csv").string() } },
        nullptr,
        init);

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();
    ASSERT_EQ(exchange.get_all_positions().size(), 1u);
    EXPECT_TRUE(exchange.get_all_positions()[0].is_long);

    ASSERT_TRUE(exchange.perp.place_order(
        "BTCUSDT",
        2.0,
        0.0,
        QTrading::Dto::Trading::OrderSide::Sell,
        QTrading::Dto::Trading::PositionSide::Both,
        true));
    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();

    EXPECT_TRUE(exchange.get_all_open_orders().empty());
    EXPECT_TRUE(exchange.get_all_positions().empty());
}

TEST_F(BinanceExchangeFixture, ReduceOnlyWithoutReduciblePositionIsRejected)
{
    WriteCsv("btc.csv", {
        { 0, 100,100,100,100,1000, 30000,100,1,0,0 }
    });

    BinanceExchange exchange = MakeExchange({
        { "BTCUSDT", (tmp_dir / "btc.csv").string() },
    });

    ASSERT_FALSE(exchange.perp.place_order(
        "BTCUSDT",
        1.0,
        0.0,
        QTrading::Dto::Trading::OrderSide::Sell,
        QTrading::Dto::Trading::PositionSide::Both,
        true));
    EXPECT_TRUE(exchange.get_all_open_orders().empty());
}

// Source case: BinanceExchangeTests.PushOnlyOnChange (trimmed to minimal no-fill contract).
TEST_F(BinanceExchangeFixture, PerpLimitOrderNoFillKeepsOpenOrder)
{
    WriteCsv("btc.csv", {
        {      0,100,100,100,100,10, 30000,100,1,0,0 },
        {  60000,100,100,100,100,10, 90000,100,1,0,0 }
    });

    BinanceExchange exchange = MakeExchange({
        { "BTCUSDT", (tmp_dir / "btc.csv").string() },
    });

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, 90.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();

    const auto& orders = exchange.get_all_open_orders();
    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(orders[0].symbol, "BTCUSDT");
    EXPECT_DOUBLE_EQ(orders[0].quantity, 1.0);
}

// Source case: AccountTests.UpdatePositionsPartialFillSameOrder (adapted to exchange step flow).
TEST_F(BinanceExchangeFixture, PerpMarketOrderPartialFillThenFullFill)
{
    WriteCsv("btc.csv", {
        {      0,100,100,100,100,2, 30000,100,1,0,0 },
        {  60000,100,100,100,100,10, 90000,100,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT", (tmp_dir / "btc.csv").string() } },
        nullptr,
        init);

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 5.0, QTrading::Dto::Trading::OrderSide::Buy));

    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();
    ASSERT_EQ(exchange.get_all_open_orders().size(), 1u);
    EXPECT_DOUBLE_EQ(exchange.get_all_open_orders()[0].quantity, 3.0);
    ASSERT_EQ(exchange.get_all_positions().size(), 1u);
    EXPECT_DOUBLE_EQ(exchange.get_all_positions()[0].quantity, 2.0);

    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();
    EXPECT_TRUE(exchange.get_all_open_orders().empty());
    ASSERT_EQ(exchange.get_all_positions().size(), 1u);
    EXPECT_DOUBLE_EQ(exchange.get_all_positions()[0].quantity, 5.0);
}

// Source case: cases A05/T14 minimal subset (spot market buy with quote-fee settlement).
TEST_F(BinanceExchangeFixture, SpotMarketBuySettlesCashInventoryAndFee)
{
    WriteCsv("btc.csv", {
        {      0,100,100,100,100,10, 30000,100,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 1000.0;
    init.perp_initial_wallet = 0.0;
    BinanceExchange exchange(
        { { "BTCUSDT", (tmp_dir / "btc.csv").string() } },
        nullptr,
        init);

    ASSERT_TRUE(exchange.spot.place_order("BTCUSDT", 1.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();

    const auto spot_balance = exchange.account.get_spot_balance();
    EXPECT_NEAR(spot_balance.WalletBalance, 899.9, 1e-9);
    ASSERT_EQ(exchange.get_all_positions().size(), 1u);
    EXPECT_EQ(exchange.get_all_positions()[0].instrument_type, QTrading::Dto::Trading::InstrumentType::Spot);
    EXPECT_NEAR(exchange.get_all_positions()[0].quantity, 1.0, 1e-12);
}

TEST_F(BinanceExchangeFixture, SpotBuyConsumesOnlySpotCash)
{
    WriteCsv("btc.csv", {
        { 0,100,100,100,100,10, 30000,100,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 1000.0;
    init.perp_initial_wallet = 500.0;
    BinanceExchange exchange = MakeExchangeWithInit({
        { "BTCUSDT",
            (tmp_dir / "btc.csv").string(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            QTrading::Dto::Trading::InstrumentType::Spot }
    }, init);

    ASSERT_TRUE(exchange.spot.place_order("BTCUSDT", 1.0, 100.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();

    EXPECT_NEAR(exchange.account.get_perp_balance().WalletBalance, 500.0, 1e-12);
    EXPECT_LT(exchange.account.get_spot_balance().WalletBalance, 1000.0);
}

TEST_F(BinanceExchangeFixture, SpotSellIncreasesOnlySpotCash)
{
    WriteCsv("btc.csv", {
        {      0,100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000,120,120,120,120,1000, 90000,100,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 2000.0;
    init.perp_initial_wallet = 500.0;
    BinanceExchange exchange = MakeExchangeWithInit({
        { "BTCUSDT",
            (tmp_dir / "btc.csv").string(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            QTrading::Dto::Trading::InstrumentType::Spot }
    }, init);

    ASSERT_TRUE(exchange.spot.place_order("BTCUSDT", 1.0, 100.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();
    const double spot_after_buy = exchange.account.get_spot_balance().WalletBalance;
    const double perp_before_sell = exchange.account.get_perp_balance().WalletBalance;

    ASSERT_TRUE(exchange.spot.place_order("BTCUSDT", 0.5, 120.0, QTrading::Dto::Trading::OrderSide::Sell));
    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();
    EXPECT_GT(exchange.account.get_spot_balance().WalletBalance, spot_after_buy);
    EXPECT_DOUBLE_EQ(exchange.account.get_perp_balance().WalletBalance, perp_before_sell);
}

TEST_F(BinanceExchangeFixture, SpotOpenOrderReservesOnlySpotBudget)
{
    WriteCsv("btc.csv", {
        { 0,100,100,100,100,1000, 30000,100,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 1000.0;
    init.perp_initial_wallet = 500.0;
    BinanceExchange exchange = MakeExchangeWithInit({
        { "BTCUSDT",
            (tmp_dir / "btc.csv").string(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            QTrading::Dto::Trading::InstrumentType::Spot }
    }, init);

    const double perp_before = exchange.account.get_perp_balance().WalletBalance;
    ASSERT_TRUE(exchange.spot.place_order("BTCUSDT", 5.0, 100.0, QTrading::Dto::Trading::OrderSide::Buy));
    EXPECT_FALSE(exchange.spot.place_order("BTCUSDT", 6.0, 100.0, QTrading::Dto::Trading::OrderSide::Buy));
    EXPECT_DOUBLE_EQ(exchange.account.get_perp_balance().WalletBalance, perp_before);
}

TEST_F(BinanceExchangeFixture, SpotBuyPlacementRejectedWhenSpotBudgetExceededByOpenOrders)
{
    WriteCsv("btc.csv", {
        { 0,100,100,100,100,1000, 30000,100,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 1000.0;
    init.perp_initial_wallet = 0.0;
    BinanceExchange exchange = MakeExchangeWithInit({
        { "BTCUSDT",
            (tmp_dir / "btc.csv").string(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            QTrading::Dto::Trading::InstrumentType::Spot }
    }, init);

    ASSERT_TRUE(exchange.spot.place_order("BTCUSDT", 8.0, 100.0, QTrading::Dto::Trading::OrderSide::Buy));
    EXPECT_FALSE(exchange.spot.place_order("BTCUSDT", 3.0, 100.0, QTrading::Dto::Trading::OrderSide::Buy));
}

TEST_F(BinanceExchangeFixture, TransferBetweenLedgersRespectsAvailableBalance)
{
    WriteCsv("btc.csv", {
        { 0,100,100,100,100,1000, 30000,100,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 500.0;
    init.perp_initial_wallet = 500.0;
    BinanceExchange exchange = MakeExchangeWithInit({
        { "BTCUSDT",
            (tmp_dir / "btc.csv").string(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            QTrading::Dto::Trading::InstrumentType::Spot }
    }, init);

    EXPECT_TRUE(exchange.account.transfer_spot_to_perp(100.0));
    EXPECT_DOUBLE_EQ(exchange.account.get_spot_balance().WalletBalance, 400.0);
    EXPECT_DOUBLE_EQ(exchange.account.get_perp_balance().WalletBalance, 600.0);

    ASSERT_TRUE(exchange.spot.place_order("BTCUSDT", 3.0, 100.0, QTrading::Dto::Trading::OrderSide::Buy));
    EXPECT_FALSE(exchange.account.transfer_spot_to_perp(150.0));

    EXPECT_TRUE(exchange.account.transfer_perp_to_spot(200.0));
    EXPECT_DOUBLE_EQ(exchange.account.get_perp_balance().WalletBalance, 400.0);
    EXPECT_DOUBLE_EQ(exchange.account.get_spot_balance().WalletBalance, 600.0);
    EXPECT_FALSE(exchange.account.transfer_perp_to_spot(1000.0));
}

// Source case: BinanceExchangeTests.SnapshotConsistent/DomainFacadeRoutesByInstrumentType (perp fill path).
TEST_F(BinanceExchangeFixture, PerpMarketBuyCreatesPositionAndDebitsFee)
{
    WriteCsv("btc.csv", {
        {      0,100,100,100,100,10, 30000,100,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT", (tmp_dir / "btc.csv").string() } },
        nullptr,
        init);

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 2.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();

    ASSERT_EQ(exchange.get_all_positions().size(), 1u);
    EXPECT_EQ(exchange.get_all_positions()[0].instrument_type, QTrading::Dto::Trading::InstrumentType::Perp);
    EXPECT_NEAR(exchange.get_all_positions()[0].quantity, 2.0, 1e-12);
    const auto perp_balance = exchange.account.get_perp_balance();
    EXPECT_NEAR(perp_balance.WalletBalance, 999.8, 1e-9);
}

TEST_F(BinanceExchangeFixture, OrderChannelPublishesWhenOrderBookChangesInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0,100,100,100,100,10, 30000,100,1,0,0 },
        {  60000,100,100,100,100,10, 90000,100,1,0,0 },
        { 120000,100,100,100,100,10,150000,100,1,0,0 },
        { 180000,100,100,100,100,10,210000,100,1,0,0 }
    });

    BinanceExchange exchange = MakeExchange({
        { "BTCUSDT", (tmp_dir / "btc.csv").string() },
    });
    auto order_channel = exchange.get_order_channel();

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, 90.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();

    auto first_snapshot = order_channel->Receive();
    ASSERT_TRUE(first_snapshot.has_value());
    ASSERT_EQ(first_snapshot->size(), 1u);

    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();
    EXPECT_FALSE(order_channel->TryReceive().has_value());

    exchange.perp.cancel_open_orders("BTCUSDT");
    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();

    auto cancel_snapshot = order_channel->Receive();
    ASSERT_TRUE(cancel_snapshot.has_value());
    EXPECT_TRUE(cancel_snapshot->empty());
}

TEST_F(BinanceExchangeFixture, FundingApplyTimingControlsSameTimestampFundingInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000, 200,200,200,200,1000, 90000,100,1,0,0 }
    });
    WriteFundingCsv("btc_funding.csv", {
        { 60000, 0.001, 100.0 }
    });

    auto run_case = [&](QTrading::Infra::Exchanges::BinanceSim::Contracts::FundingApplyTiming timing) -> double {
        Account::AccountInitConfig init{};
        init.spot_initial_cash = 0.0;
        init.perp_initial_wallet = 1000.0;
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "btc.csv").string(), std::optional<std::string>((tmp_dir / "btc_funding.csv").string()) } },
            nullptr,
            init);

        BinanceExchange::SimulationConfig cfg = exchange.simulation_config();
        cfg.funding_apply_timing = timing;
        exchange.apply_simulation_config(cfg);

        auto market_channel = exchange.get_market_channel();
        if (!exchange.step()) {
            ADD_FAILURE() << "step#1 failed";
            return std::numeric_limits<double>::quiet_NaN();
        }
        (void)market_channel->Receive();
        if (!exchange.perp.place_order("BTCUSDT", 1.0, QTrading::Dto::Trading::OrderSide::Buy)) {
            ADD_FAILURE() << "place_order failed";
            return std::numeric_limits<double>::quiet_NaN();
        }
        if (!exchange.step()) {
            ADD_FAILURE() << "step#2 failed";
            return std::numeric_limits<double>::quiet_NaN();
        }
        (void)market_channel->Receive();
        BinanceExchange::StatusSnapshot snapshot{};
        exchange.FillStatusSnapshot(snapshot);
        return snapshot.wallet_balance;
    };

    const double before = run_case(QTrading::Infra::Exchanges::BinanceSim::Contracts::FundingApplyTiming::BeforeMatching);
    const double after = run_case(QTrading::Infra::Exchanges::BinanceSim::Contracts::FundingApplyTiming::AfterMatching);
    EXPECT_NEAR(before - after, 0.1, 1e-6);
}

TEST_F(BinanceExchangeFixture, FundingAppliedAndDedupedInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000, 110,110,110,110,1000, 90000,100,1,0,0 },
        { 120000, 120,120,120,120,1000,150000,100,1,0,0 }
    });
    WriteCompactCsv("btc_mark.csv", {
        {      0, 130,130,130,130,  30000 },
        {  60000, 140,140,140,140,  90000 },
        { 120000, 150,150,150,150, 150000 }
    });
    WriteFundingCsv("btc_funding.csv", {
        {  60000,  0.001, std::nullopt },
        { 120000, -0.002, 100.0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT",
            (tmp_dir / "btc.csv").string(),
            std::optional<std::string>((tmp_dir / "btc_funding.csv").string()),
            std::optional<std::string>((tmp_dir / "btc_mark.csv").string()) } },
        nullptr,
        init);

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, QTrading::Dto::Trading::OrderSide::Buy));
    auto market_channel = exchange.get_market_channel();

    BinanceExchange::StatusSnapshot snapshot{};
    ASSERT_TRUE(exchange.step());
    (void)market_channel->Receive();
    exchange.FillStatusSnapshot(snapshot);
    const double base = snapshot.wallet_balance;

    ASSERT_TRUE(exchange.step());
    (void)market_channel->Receive();
    exchange.FillStatusSnapshot(snapshot);
    const double after1 = snapshot.wallet_balance;
    EXPECT_NEAR(after1 - base, -0.14, 1e-6);

    ASSERT_TRUE(exchange.step());
    (void)market_channel->Receive();
    exchange.FillStatusSnapshot(snapshot);
    const double after2 = snapshot.wallet_balance;
    EXPECT_NEAR(after2 - base, 0.06, 1e-6);

    EXPECT_FALSE(exchange.step());
}

TEST_F(BinanceExchangeFixture, ApplyFundingLongPays)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000, 100,100,100,100,1000, 90000,100,1,0,0 }
    });
    WriteFundingCsv("btc_funding.csv", {
        { 60000, 0.001, 10000.0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT", (tmp_dir / "btc.csv").string(), std::optional<std::string>((tmp_dir / "btc_funding.csv").string()) } },
        nullptr,
        init);

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, QTrading::Dto::Trading::OrderSide::Buy));
    auto market_channel = exchange.get_market_channel();

    BinanceExchange::StatusSnapshot snapshot{};
    ASSERT_TRUE(exchange.step());
    (void)market_channel->Receive();
    exchange.FillStatusSnapshot(snapshot);
    const double before_funding = snapshot.wallet_balance;

    ASSERT_TRUE(exchange.step());
    (void)market_channel->Receive();
    exchange.FillStatusSnapshot(snapshot);
    EXPECT_NEAR(snapshot.wallet_balance - before_funding, -10.0, 1e-6);
}

TEST_F(BinanceExchangeFixture, ApplyFundingShortReceives)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000, 100,100,100,100,1000, 90000,100,1,0,0 }
    });
    WriteFundingCsv("btc_funding.csv", {
        { 60000, 0.001, 10000.0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT", (tmp_dir / "btc.csv").string(), std::optional<std::string>((tmp_dir / "btc_funding.csv").string()) } },
        nullptr,
        init);

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, QTrading::Dto::Trading::OrderSide::Sell));
    auto market_channel = exchange.get_market_channel();

    BinanceExchange::StatusSnapshot snapshot{};
    ASSERT_TRUE(exchange.step());
    (void)market_channel->Receive();
    exchange.FillStatusSnapshot(snapshot);
    const double before_funding = snapshot.wallet_balance;

    ASSERT_TRUE(exchange.step());
    (void)market_channel->Receive();
    exchange.FillStatusSnapshot(snapshot);
    EXPECT_NEAR(snapshot.wallet_balance - before_funding, 10.0, 1e-6);
}

TEST_F(BinanceExchangeFixture, FundingWithoutMarkSourceIsSkippedInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000, 100,100,100,100,1000, 90000,100,1,0,0 }
    });
    WriteFundingCsv("btc_funding.csv", {
        { 60000, 0.001, std::nullopt }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT", (tmp_dir / "btc.csv").string(), std::optional<std::string>((tmp_dir / "btc_funding.csv").string()) } },
        nullptr,
        init);

    auto market_channel = exchange.get_market_channel();
    ASSERT_TRUE(exchange.step());
    (void)market_channel->Receive();
    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.step());
    (void)market_channel->Receive();

    BinanceExchange::StatusSnapshot snapshot{};
    exchange.FillStatusSnapshot(snapshot);
    EXPECT_EQ(snapshot.funding_applied_events, 0u);
    EXPECT_GE(snapshot.funding_skipped_no_mark, 1u);
}

TEST_F(BinanceExchangeFixture, NoFundingPathKeepsBalanceInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000, 110,110,110,110,1000, 90000,100,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT", (tmp_dir / "btc.csv").string() } },
        nullptr,
        init);

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, QTrading::Dto::Trading::OrderSide::Buy));
    auto market_channel = exchange.get_market_channel();

    BinanceExchange::StatusSnapshot snapshot{};
    ASSERT_TRUE(exchange.step());
    (void)market_channel->Receive();
    exchange.FillStatusSnapshot(snapshot);
    const double base = snapshot.wallet_balance;

    ASSERT_TRUE(exchange.step());
    (void)market_channel->Receive();
    exchange.FillStatusSnapshot(snapshot);
    const double after = snapshot.wallet_balance;

    EXPECT_NEAR(after, base, 1e-8);
}

TEST_F(BinanceExchangeFixture, FundingAppliedEventsCountAffectedPerpPositionsInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        { 120000, 110,110,110,110,1000,150000,100,1,0,0 }
    });
    WriteCsv("eth.csv", {
        {      0, 200,200,200,200,1000, 30000,100,1,0,0 },
        { 120000, 210,210,210,210,1000,150000,100,1,0,0 }
    });
    WriteFundingCsv("btc_funding.csv", {
        { 60000, 0.001, 100.0 }
    });
    WriteFundingCsv("eth_funding.csv", {
        { 60000, 0.001, 200.0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        {
            { "BTCUSDT", (tmp_dir / "btc.csv").string(), std::optional<std::string>((tmp_dir / "btc_funding.csv").string()) },
            { "ETHUSDT", (tmp_dir / "eth.csv").string(), std::optional<std::string>((tmp_dir / "eth_funding.csv").string()) }
        },
        nullptr,
        init);

    auto market_channel = exchange.get_market_channel();
    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.perp.place_order("ETHUSDT", 1.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.step());
    (void)market_channel->Receive();
    ASSERT_EQ(exchange.get_all_positions().size(), 2u);

    ASSERT_TRUE(exchange.step());
    auto funding_step = market_channel->Receive();
    ASSERT_TRUE(funding_step.has_value());
    EXPECT_EQ(funding_step->get()->Timestamp, 60000u);

    BinanceExchange::StatusSnapshot snapshot{};
    exchange.FillStatusSnapshot(snapshot);
    // Current reduced kernel keeps one perp position per symbol; this assertion
    // locks event counting to "affected position records", not a single batch event.
    EXPECT_EQ(snapshot.funding_applied_events, 2u);
}

TEST_F(BinanceExchangeFixture, FundingTimestampParticipatesInStepTimelineInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        { 120000, 110,110,110,110,1000,150000,100,1,0,0 }
    });
    WriteFundingCsv("btc_funding.csv", {
        { 60000, 0.001, 100.0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT", (tmp_dir / "btc.csv").string(), std::optional<std::string>((tmp_dir / "btc_funding.csv").string()) } },
        nullptr,
        init);

    auto market_channel = exchange.get_market_channel();
    ASSERT_TRUE(exchange.step());
    auto step1 = market_channel->Receive();
    ASSERT_TRUE(step1.has_value());
    EXPECT_EQ(step1->get()->Timestamp, 0u);
    ASSERT_TRUE(exchange.step());
    auto step2 = market_channel->Receive();
    ASSERT_TRUE(step2.has_value());
    EXPECT_EQ(step2->get()->Timestamp, 60000u);
    ASSERT_EQ(step2->get()->funding_by_id.size(), 1u);
    EXPECT_TRUE(step2->get()->funding_by_id[0].has_value());
    EXPECT_FALSE(step2->get()->trade_klines_by_id[0].has_value());
    ASSERT_TRUE(exchange.step());
    auto step3 = market_channel->Receive();
    ASSERT_TRUE(step3.has_value());
    EXPECT_EQ(step3->get()->Timestamp, 120000u);
}

TEST_F(BinanceExchangeFixture, FundingTimelineUsesEarliestDueTimestampAcrossSymbolsInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        { 120000, 101,101,101,101,1000,150000,100,1,0,0 }
    });
    WriteCsv("eth.csv", {
        {      0, 200,200,200,200,1000, 30000,100,1,0,0 },
        { 120000, 201,201,201,201,1000,150000,100,1,0,0 }
    });
    WriteFundingCsv("btc_funding.csv", {
        { 30000, 0.001, 100.0 }
    });
    WriteFundingCsv("eth_funding.csv", {
        { 60000, 0.001, 200.0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        {
            { "BTCUSDT", (tmp_dir / "btc.csv").string(), std::optional<std::string>((tmp_dir / "btc_funding.csv").string()) },
            { "ETHUSDT", (tmp_dir / "eth.csv").string(), std::optional<std::string>((tmp_dir / "eth_funding.csv").string()) }
        },
        nullptr,
        init);

    auto market_channel = exchange.get_market_channel();
    ASSERT_TRUE(exchange.step());
    auto step1 = market_channel->Receive();
    ASSERT_TRUE(step1.has_value());
    EXPECT_EQ(step1->get()->Timestamp, 0u);

    ASSERT_TRUE(exchange.step());
    auto step2 = market_channel->Receive();
    ASSERT_TRUE(step2.has_value());
    EXPECT_EQ(step2->get()->Timestamp, 30000u);
    ASSERT_EQ(step2->get()->funding_by_id.size(), 2u);
    EXPECT_TRUE(step2->get()->funding_by_id[0].has_value());
    EXPECT_FALSE(step2->get()->funding_by_id[1].has_value());

    ASSERT_TRUE(exchange.step());
    auto step3 = market_channel->Receive();
    ASSERT_TRUE(step3.has_value());
    EXPECT_EQ(step3->get()->Timestamp, 60000u);
    ASSERT_EQ(step3->get()->funding_by_id.size(), 2u);
    EXPECT_FALSE(step3->get()->funding_by_id[0].has_value());
    EXPECT_TRUE(step3->get()->funding_by_id[1].has_value());

    ASSERT_TRUE(exchange.step());
    auto step4 = market_channel->Receive();
    ASSERT_TRUE(step4.has_value());
    EXPECT_EQ(step4->get()->Timestamp, 120000u);
}

TEST_F(BinanceExchangeFixture, FundingUsesRawMarkDatasetWhenFundingMarkMissingInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        { 120000, 120,120,120,120,1000,150000,100,1,0,0 }
    });
    WriteCsv("btc_mark.csv", {
        {  60000, 110,110,110,110,1000, 90000,100,1,0,0 }
    });
    WriteFundingCsv("btc_funding.csv", {
        { 60000, 0.001, std::nullopt }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT",
            (tmp_dir / "btc.csv").string(),
            std::optional<std::string>((tmp_dir / "btc_funding.csv").string()),
            std::optional<std::string>((tmp_dir / "btc_mark.csv").string()) } },
        nullptr,
        init);

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, QTrading::Dto::Trading::OrderSide::Buy));
    auto market_channel = exchange.get_market_channel();
    ASSERT_TRUE(exchange.step());
    (void)market_channel->Receive();
    BinanceExchange::StatusSnapshot snapshot{};
    exchange.FillStatusSnapshot(snapshot);
    const double wallet_after_entry = snapshot.wallet_balance;

    ASSERT_TRUE(exchange.step());
    auto funding_step = market_channel->Receive();
    ASSERT_TRUE(funding_step.has_value());
    EXPECT_EQ(funding_step->get()->Timestamp, 60000u);
    ASSERT_EQ(funding_step->get()->mark_klines_by_id.size(), 1u);
    ASSERT_TRUE(funding_step->get()->mark_klines_by_id[0].has_value());

    exchange.FillStatusSnapshot(snapshot);
    EXPECT_NEAR(snapshot.wallet_balance - wallet_after_entry, -0.11, 1e-6);
    EXPECT_EQ(snapshot.funding_skipped_no_mark, 0u);
}

TEST_F(BinanceExchangeFixture, FundingMarkMissingWithoutExactRawMarkStillSkipsInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        { 120000, 120,120,120,120,1000,150000,100,1,0,0 }
    });
    WriteCsv("btc_mark.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        { 120000, 120,120,120,120,1000,150000,100,1,0,0 }
    });
    WriteFundingCsv("btc_funding.csv", {
        { 60000, 0.001, std::nullopt }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT",
            (tmp_dir / "btc.csv").string(),
            std::optional<std::string>((tmp_dir / "btc_funding.csv").string()),
            std::optional<std::string>((tmp_dir / "btc_mark.csv").string()) } },
        nullptr,
        init);

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, QTrading::Dto::Trading::OrderSide::Buy));
    auto market_channel = exchange.get_market_channel();
    ASSERT_TRUE(exchange.step());
    (void)market_channel->Receive();
    BinanceExchange::StatusSnapshot snapshot{};
    exchange.FillStatusSnapshot(snapshot);
    const double wallet_after_entry = snapshot.wallet_balance;

    ASSERT_TRUE(exchange.step());
    auto funding_step = market_channel->Receive();
    ASSERT_TRUE(funding_step.has_value());
    EXPECT_EQ(funding_step->get()->Timestamp, 60000u);
    ASSERT_EQ(funding_step->get()->mark_klines_by_id.size(), 1u);
    EXPECT_FALSE(funding_step->get()->mark_klines_by_id[0].has_value());

    exchange.FillStatusSnapshot(snapshot);
    EXPECT_NEAR(snapshot.wallet_balance - wallet_after_entry, 0.0, 1e-9);
    EXPECT_GE(snapshot.funding_skipped_no_mark, 1u);
}

TEST_F(BinanceExchangeFixture, StatusSnapshotExposesRawMarkIndexAndBasisWarningInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000, 101,101,101,101,1000, 90000,100,1,0,0 }
    });
    WriteCsv("btc_mark.csv", {
        {      0, 123,123,123,123,1000, 30000,100,1,0,0 }
    });
    WriteCsv("btc_index.csv", {
        {      0, 121,121,121,121,1000, 30000,100,1,0,0 }
    });

    BinanceExchange exchange(
        { { "BTCUSDT",
            (tmp_dir / "btc.csv").string(),
            std::nullopt,
            std::optional<std::string>((tmp_dir / "btc_mark.csv").string()),
            std::optional<std::string>((tmp_dir / "btc_index.csv").string()) } },
        nullptr,
        QTrading::Infra::Exchanges::BinanceSim::Support::BuildInitConfig(1000.0, 0));

    auto cfg = exchange.simulation_config();
    cfg.basis_warning_bps = 100.0;
    exchange.apply_simulation_config(cfg);

    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();

    BinanceExchange::StatusSnapshot snapshot{};
    exchange.FillStatusSnapshot(snapshot);
    ASSERT_EQ(snapshot.prices.size(), 1u);
    const auto& price = snapshot.prices[0];
    EXPECT_TRUE(price.has_mark_price);
    EXPECT_NEAR(price.mark_price, 123.0, 1e-12);
    EXPECT_EQ(price.mark_price_source,
        static_cast<int32_t>(QTrading::Infra::Exchanges::BinanceSim::Contracts::ReferencePriceSource::Raw));
    EXPECT_TRUE(price.has_index_price);
    EXPECT_NEAR(price.index_price, 121.0, 1e-12);
    EXPECT_EQ(price.index_price_source,
        static_cast<int32_t>(QTrading::Infra::Exchanges::BinanceSim::Contracts::ReferencePriceSource::Raw));
    EXPECT_EQ(snapshot.basis_warning_symbols, 1u);
}

TEST_F(BinanceExchangeFixture, StatusSnapshotSuppressesBasisWarningWhenMarkIndexTimestampsAreIncoherentInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000, 101,101,101,101,1000, 90000,100,1,0,0 }
    });
    WriteCsv("btc_mark.csv", {
        {      0, 123,123,123,123,1000, 30000,100,1,0,0 }
    });
    WriteCsv("btc_index.csv", {
        {  60000, 121,121,121,121,1000, 90000,100,1,0,0 }
    });

    BinanceExchange exchange(
        { { "BTCUSDT",
            (tmp_dir / "btc.csv").string(),
            std::nullopt,
            std::optional<std::string>((tmp_dir / "btc_mark.csv").string()),
            std::optional<std::string>((tmp_dir / "btc_index.csv").string()) } },
        nullptr,
        QTrading::Infra::Exchanges::BinanceSim::Support::BuildInitConfig(1000.0, 0));

    auto cfg = exchange.simulation_config();
    cfg.basis_warning_bps = 100.0;
    exchange.apply_simulation_config(cfg);

    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();
    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();

    BinanceExchange::StatusSnapshot snapshot{};
    exchange.FillStatusSnapshot(snapshot);
    ASSERT_EQ(snapshot.prices.size(), 1u);
    EXPECT_TRUE(snapshot.prices[0].has_mark_price);
    EXPECT_TRUE(snapshot.prices[0].has_index_price);
    EXPECT_EQ(snapshot.basis_warning_symbols, 0u);
}

TEST_F(BinanceExchangeFixture, StatusSnapshotKeepsStressTierAndOpeningBlockDiagnosticsDisabledInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000, 101,101,101,101,1000, 90000,100,1,0,0 }
    });
    WriteCsv("btc_mark.csv", {
        {      0, 130,130,130,130,1000, 30000,100,1,0,0 }
    });
    WriteCsv("btc_index.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT",
            (tmp_dir / "btc.csv").string(),
            std::nullopt,
            std::optional<std::string>((tmp_dir / "btc_mark.csv").string()),
            std::optional<std::string>((tmp_dir / "btc_index.csv").string()) } },
        nullptr,
        init);

    auto cfg = exchange.simulation_config();
    cfg.basis_warning_bps = 100.0;
    cfg.basis_stress_bps = 150.0;
    cfg.simulator_risk_overlay_enabled = true;
    cfg.basis_risk_guard_enabled = true;
    cfg.basis_stress_blocks_opening_orders = true;
    cfg.basis_stress_cap = 1.0;
    exchange.apply_simulation_config(cfg);

    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();

    BinanceExchange::StatusSnapshot snapshot{};
    exchange.FillStatusSnapshot(snapshot);
    EXPECT_EQ(snapshot.basis_warning_symbols, 1u);
    EXPECT_EQ(snapshot.basis_stress_symbols, 0u);
    EXPECT_EQ(snapshot.basis_stress_blocked_orders, 0u);

    EXPECT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();
    ASSERT_EQ(exchange.get_all_positions().size(), 1u);
    EXPECT_NEAR(exchange.get_all_positions()[0].quantity, 1.0, 1e-12);
}

TEST_F(BinanceExchangeFixture, LiquidationDistressCancelsPerpOrdersAndReducesExposureWithoutBankruptcyResetInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,10000, 30000,10000,1,0,0 }
    });
    WriteCsv("btc_mark.csv", {
        {      0, 10,10,10,10,10000, 30000,10000,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT",
            (tmp_dir / "btc.csv").string(),
            std::nullopt,
            std::optional<std::string>((tmp_dir / "btc_mark.csv").string()) } },
        nullptr,
        init);

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 500.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.perp.place_order(
        "BTCUSDT", 1.0, 1.0, QTrading::Dto::Trading::OrderSide::Buy));

    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();

    BinanceExchange::StatusSnapshot snapshot{};
    exchange.FillStatusSnapshot(snapshot);
    EXPECT_EQ(exchange.get_all_positions().size(), 0u);
    EXPECT_EQ(exchange.get_all_open_orders().size(), 0u);
    EXPECT_NEAR(snapshot.wallet_balance, -44055.0, 1e-6);
}

TEST_F(BinanceExchangeFixture, LiquidationRemainsNoOpWithoutDistressInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,10000, 30000,10000,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT", (tmp_dir / "btc.csv").string() } },
        nullptr,
        init);

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, QTrading::Dto::Trading::OrderSide::Buy));

    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();

    BinanceExchange::StatusSnapshot snapshot{};
    exchange.FillStatusSnapshot(snapshot);
    ASSERT_EQ(exchange.get_all_positions().size(), 1u);
    EXPECT_NEAR(exchange.get_all_positions()[0].quantity, 1.0, 1e-12);
    EXPECT_NEAR(snapshot.wallet_balance, 999.9, 1e-9);
}

TEST_F(BinanceExchangeFixture, LiquidationEligibilityCheckDoesNotMutatePositionFieldsWithoutReductionInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,10000, 30000,10000,1,0,0 },
        {  60000, 100,100,100,100,10000, 90000,10000,1,0,0 }
    });
    WriteCsv("btc_mark.csv", {
        {  60000, 50,50,50,50,10000, 90000,10000,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT",
            (tmp_dir / "btc.csv").string(),
            std::nullopt,
            std::optional<std::string>((tmp_dir / "btc_mark.csv").string()) } },
        nullptr,
        init);

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();
    ASSERT_EQ(exchange.get_all_positions().size(), 1u);

    const auto& before_eval = exchange.get_all_positions().front();
    EXPECT_NEAR(before_eval.unrealized_pnl, 0.0, 1e-12);
    EXPECT_NEAR(before_eval.notional, 100.0, 1e-12);
    EXPECT_NEAR(before_eval.maintenance_margin, 0.0, 1e-12);

    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();
    ASSERT_EQ(exchange.get_all_positions().size(), 1u);
    const auto& after_eval = exchange.get_all_positions().front();
    EXPECT_NEAR(after_eval.unrealized_pnl, 0.0, 1e-12);
    EXPECT_NEAR(after_eval.notional, 100.0, 1e-12);
    EXPECT_NEAR(after_eval.maintenance_margin, 0.0, 1e-12);
}

TEST_F(BinanceExchangeFixture, LiquidationSkipsWhenRawMarkContextMissingInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,10000, 30000,10000,1,0,0 },
        {  60000, 10,10,10,10,10000, 90000,10000,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT", (tmp_dir / "btc.csv").string() } },
        nullptr,
        init);

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 500.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();

    ASSERT_EQ(exchange.get_all_positions().size(), 1u);
    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();

    BinanceExchange::StatusSnapshot snapshot{};
    exchange.FillStatusSnapshot(snapshot);
    EXPECT_EQ(exchange.get_all_positions().size(), 1u);
    EXPECT_NEAR(exchange.get_all_positions()[0].quantity, 500.0, 1e-9);
    EXPECT_EQ(exchange.get_all_open_orders().size(), 0u);
    EXPECT_NEAR(snapshot.wallet_balance, 950.0, 1e-9);
}

TEST_F(BinanceExchangeLogFixture, ReducedObservabilityStatusPrecedesEventRowsInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000, 101,101,101,101,1000, 90000,100,1,0,0 }
    });
    WriteFundingCsv("btc_funding.csv", {
        { 60000, 0.001, 100.0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT", (tmp_dir / "btc.csv").string(), std::optional<std::string>((tmp_dir / "btc_funding.csv").string()) } },
        logger,
        init,
        77u);

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();
    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();
    StopLogger();

    const auto account_rows = FilterRowsByModule(QTrading::Log::LogModule::Account);
    const auto market_rows = FilterRowsByModule(QTrading::Log::LogModule::MarketEvent);
    ASSERT_FALSE(account_rows.empty());
    ASSERT_FALSE(market_rows.empty());
    const auto funding_rows = FilterRowsByModule(QTrading::Log::LogModule::FundingEvent);
    ASSERT_FALSE(funding_rows.empty());

    auto first_account_arrival_by_ts = std::unordered_map<uint64_t, size_t>{};
    first_account_arrival_by_ts.reserve(account_rows.size());
    for (const auto& row_view : account_rows) {
        ASSERT_NE(row_view.row, nullptr);
        const uint64_t ts = row_view.row->ts;
        const auto it = first_account_arrival_by_ts.find(ts);
        if (it == first_account_arrival_by_ts.end() || row_view.arrival_index < it->second) {
            first_account_arrival_by_ts[ts] = row_view.arrival_index;
        }
    }

    for (const auto& row_view : market_rows) {
        ASSERT_NE(row_view.row, nullptr);
        const auto it = first_account_arrival_by_ts.find(row_view.row->ts);
        ASSERT_NE(it, first_account_arrival_by_ts.end());
        EXPECT_LT(it->second, row_view.arrival_index);
    }
    for (const auto& row_view : funding_rows) {
        ASSERT_NE(row_view.row, nullptr);
        const auto it = first_account_arrival_by_ts.find(row_view.row->ts);
        ASSERT_NE(it, first_account_arrival_by_ts.end());
        EXPECT_LT(it->second, row_view.arrival_index);
    }
}

TEST_F(BinanceExchangeLogFixture, ReducedObservabilityFundingOnlyStepCarriesStepContextInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        { 120000, 110,110,110,110,1000,150000,100,1,0,0 }
    });
    WriteFundingCsv("btc_funding.csv", {
        { 60000, 0.001, 100.0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT", (tmp_dir / "btc.csv").string(), std::optional<std::string>((tmp_dir / "btc_funding.csv").string()) } },
        logger,
        init,
        88u);

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();
    ASSERT_TRUE(exchange.step());
    auto funding_step = exchange.get_market_channel()->Receive();
    ASSERT_TRUE(funding_step.has_value());
    EXPECT_EQ(funding_step->get()->Timestamp, 60000u);
    StopLogger();

    const auto market_rows = FilterRowsByModule(QTrading::Log::LogModule::MarketEvent);
    ASSERT_GE(market_rows.size(), 2u);
    const auto* market_payload = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::MarketEventDto>(market_rows[1].row);
    ASSERT_NE(market_payload, nullptr);
    EXPECT_EQ(market_payload->run_id, 88u);
    EXPECT_EQ(market_payload->step_seq, 2u);
    EXPECT_EQ(market_payload->ts_local, 60000u);
    EXPECT_FALSE(market_payload->has_kline);

    const auto funding_rows = FilterRowsByModule(QTrading::Log::LogModule::FundingEvent);
    ASSERT_FALSE(funding_rows.empty());
    const auto* funding_payload = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::FundingEventDto>(funding_rows.front().row);
    ASSERT_NE(funding_payload, nullptr);
    EXPECT_EQ(funding_payload->run_id, 88u);
    EXPECT_EQ(funding_payload->step_seq, 2u);
    EXPECT_EQ(funding_payload->ts_local, 60000u);
    EXPECT_EQ(funding_payload->symbol, "BTCUSDT");

    EXPECT_TRUE(FilterRowsByModule(QTrading::Log::LogModule::AccountEvent).empty());
    EXPECT_TRUE(FilterRowsByModule(QTrading::Log::LogModule::PositionEvent).empty());
    EXPECT_TRUE(FilterRowsByModule(QTrading::Log::LogModule::OrderEvent).empty());
}

TEST_F(BinanceExchangeLogFixture, FundingEventUsesResolvedRawMarkFallbackConsistentlyInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        { 120000, 120,120,120,120,1000,150000,100,1,0,0 }
    });
    WriteCsv("btc_mark.csv", {
        {  60000, 110,110,110,110,1000, 90000,100,1,0,0 }
    });
    WriteFundingCsv("btc_funding.csv", {
        { 60000, 0.001, std::nullopt }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT",
            (tmp_dir / "btc.csv").string(),
            std::optional<std::string>((tmp_dir / "btc_funding.csv").string()),
            std::optional<std::string>((tmp_dir / "btc_mark.csv").string()) } },
        logger,
        init,
        123u);

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();
    ASSERT_TRUE(exchange.step());
    auto funding_step = exchange.get_market_channel()->Receive();
    ASSERT_TRUE(funding_step.has_value());
    EXPECT_EQ(funding_step->get()->Timestamp, 60000u);
    StopLogger();

    const auto funding_rows = FilterRowsByModule(QTrading::Log::LogModule::FundingEvent);
    ASSERT_FALSE(funding_rows.empty());

    const QTrading::Log::FileLogger::FeatherV2::FundingEventDto* matched_payload = nullptr;
    for (const auto& row_view : funding_rows) {
        ASSERT_NE(row_view.row, nullptr);
        if (row_view.row->ts != 60000u) {
            continue;
        }
        const auto* payload = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::FundingEventDto>(row_view.row);
        if (payload == nullptr || payload->symbol != "BTCUSDT") {
            continue;
        }
        matched_payload = payload;
        break;
    }

    ASSERT_NE(matched_payload, nullptr);
    EXPECT_TRUE(matched_payload->has_mark_price);
    EXPECT_EQ(matched_payload->skip_reason, 0);
    EXPECT_NEAR(matched_payload->mark_price, 110.0, 1e-12);
    EXPECT_EQ(
        matched_payload->mark_price_source,
        static_cast<int32_t>(QTrading::Infra::Exchanges::BinanceSim::Contracts::ReferencePriceSource::Raw));
}

TEST_F(BinanceExchangeLogFixture, FundingEventSkipsWhenOnlyInterpolableMarkExistsInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        { 120000, 120,120,120,120,1000,150000,100,1,0,0 }
    });
    WriteCsv("btc_mark.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        { 120000, 120,120,120,120,1000,150000,100,1,0,0 }
    });
    WriteFundingCsv("btc_funding.csv", {
        { 60000, 0.001, std::nullopt }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT",
            (tmp_dir / "btc.csv").string(),
            std::optional<std::string>((tmp_dir / "btc_funding.csv").string()),
            std::optional<std::string>((tmp_dir / "btc_mark.csv").string()) } },
        logger,
        init,
        404u);

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.step());
    ASSERT_TRUE(exchange.get_market_channel()->Receive().has_value());
    ASSERT_TRUE(exchange.step());
    auto funding_step = exchange.get_market_channel()->Receive();
    ASSERT_TRUE(funding_step.has_value());
    EXPECT_EQ(funding_step->get()->Timestamp, 60000u);
    StopLogger();

    const auto funding_rows = FilterRowsByModule(QTrading::Log::LogModule::FundingEvent);
    ASSERT_EQ(funding_rows.size(), 1u);
    const auto* payload = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::FundingEventDto>(funding_rows.front().row);
    ASSERT_NE(payload, nullptr);
    EXPECT_FALSE(payload->has_mark_price);
    EXPECT_EQ(payload->skip_reason, 1);
    EXPECT_EQ(
        payload->mark_price_source,
        static_cast<int32_t>(QTrading::Infra::Exchanges::BinanceSim::Contracts::ReferencePriceSource::None));
}

TEST_F(BinanceExchangeLogFixture, ReducedObservabilityStatusVersionGateDoesNotSuppressMarketEventsInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000, 101,101,101,101,1000, 90000,100,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 1000.0;
    init.perp_initial_wallet = 0.0;
    BinanceExchange exchange(
        { { "BTCUSDT", (tmp_dir / "btc.csv").string() } },
        logger,
        init,
        99u);

    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();
    ASSERT_TRUE(exchange.step());
    (void)exchange.get_market_channel()->Receive();
    StopLogger();

    const auto account_rows = FilterRowsByModule(QTrading::Log::LogModule::Account);
    ASSERT_EQ(account_rows.size(), 1u);
    EXPECT_EQ(account_rows[0].row->ts, 0u);

    const auto market_rows = FilterRowsByModule(QTrading::Log::LogModule::MarketEvent);
    ASSERT_EQ(market_rows.size(), 2u);
    const auto* event0 = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::MarketEventDto>(market_rows[0].row);
    const auto* event1 = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::MarketEventDto>(market_rows[1].row);
    ASSERT_NE(event0, nullptr);
    ASSERT_NE(event1, nullptr);
    EXPECT_EQ(event0->step_seq, 1u);
    EXPECT_EQ(event1->step_seq, 2u);

    EXPECT_TRUE(FilterRowsByModule(QTrading::Log::LogModule::AccountEvent).empty());
    EXPECT_TRUE(FilterRowsByModule(QTrading::Log::LogModule::PositionEvent).empty());
    EXPECT_TRUE(FilterRowsByModule(QTrading::Log::LogModule::OrderEvent).empty());
}

TEST_F(BinanceExchangeLogFixture, ReducedObservabilityMarketEventCarriesRawMarkIndexContextInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,101,99,100.5,1000, 30000,1000,1,0,0 }
    });
    WriteCsv("btc_mark.csv", {
        {      0, 123,123,123,123,1000, 30000,1000,1,0,0 }
    });
    WriteCsv("btc_index.csv", {
        {      0, 117,117,117,117,1000, 30000,1000,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 1000.0;
    init.perp_initial_wallet = 0.0;
    BinanceExchange exchange(
        { { "BTCUSDT",
            (tmp_dir / "btc.csv").string(),
            std::nullopt,
            std::optional<std::string>((tmp_dir / "btc_mark.csv").string()),
            std::optional<std::string>((tmp_dir / "btc_index.csv").string()) } },
        logger,
        init,
        321u);

    ASSERT_TRUE(exchange.step());
    ASSERT_TRUE(exchange.get_market_channel()->Receive().has_value());
    StopLogger();

    const auto market_rows = FilterRowsByModule(QTrading::Log::LogModule::MarketEvent);
    ASSERT_EQ(market_rows.size(), 1u);
    const auto* payload = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::MarketEventDto>(market_rows.front().row);
    ASSERT_NE(payload, nullptr);
    EXPECT_TRUE(payload->has_mark_price);
    EXPECT_DOUBLE_EQ(payload->mark_price, 123.0);
    EXPECT_EQ(payload->mark_price_source,
        static_cast<int32_t>(QTrading::Infra::Exchanges::BinanceSim::Contracts::ReferencePriceSource::Raw));
    EXPECT_TRUE(payload->has_index_price);
    EXPECT_DOUBLE_EQ(payload->index_price, 117.0);
    EXPECT_EQ(payload->index_price_source,
        static_cast<int32_t>(QTrading::Infra::Exchanges::BinanceSim::Contracts::ReferencePriceSource::Raw));
}

TEST_F(BinanceExchangeLogFixture, ReducedObservabilityMarketThenFundingOrderWithinSingleStepInCurrentKernel)
{
    WriteCsv("btc.csv", {
        {      0, 100,101,99,100,1000, 30000,1000,1,0,0 },
        {  60000, 101,102,100,101,1000, 90000,1000,1,0,0 }
    });
    WriteFundingCsv("btc_funding.csv", {
        { 60000, 0.0001, 101.0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT",
            (tmp_dir / "btc.csv").string(),
            std::optional<std::string>((tmp_dir / "btc_funding.csv").string()) } },
        logger,
        init,
        333u);

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.step());
    ASSERT_TRUE(exchange.get_market_channel()->Receive().has_value());
    ASSERT_TRUE(exchange.step());
    ASSERT_TRUE(exchange.get_market_channel()->Receive().has_value());
    StopLogger();

    const auto market_rows = FilterRowsByModule(QTrading::Log::LogModule::MarketEvent);
    const auto funding_rows = FilterRowsByModule(QTrading::Log::LogModule::FundingEvent);
    ASSERT_EQ(funding_rows.size(), 1u);

    const ArrivedRowView* market_step2 = nullptr;
    for (const auto& row_view : market_rows) {
        const auto* market = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::MarketEventDto>(row_view.row);
        ASSERT_NE(market, nullptr);
        if (market->step_seq == 2u) {
            market_step2 = &row_view;
            break;
        }
    }

    ASSERT_NE(market_step2, nullptr);
    const auto* market_payload = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::MarketEventDto>(market_step2->row);
    const auto* funding_payload = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::FundingEventDto>(funding_rows.front().row);
    ASSERT_NE(market_payload, nullptr);
    ASSERT_NE(funding_payload, nullptr);
    EXPECT_EQ(market_payload->step_seq, 2u);
    EXPECT_EQ(funding_payload->step_seq, 2u);
    EXPECT_LT(market_step2->arrival_index, funding_rows.front().arrival_index);
    EXPECT_LT(market_payload->event_seq, funding_payload->event_seq);
}

TEST_F(BinanceExchangeLogFixture, ReducedObservabilityKeepsAccountPositionOrderEventModulesDisabledInCurrentKernel)
{
    WriteCsv("event_order_fill.csv", {
        {      0, 100.0,100.0,100.0,100.0,1000.0, 30000,1000.0,1,0.0,0.0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT", (tmp_dir / "event_order_fill.csv").string() } },
        logger,
        init,
        7200u);

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.step());
    ASSERT_TRUE(exchange.get_market_channel()->Receive().has_value());
    StopLogger();

    ASSERT_FALSE(FilterRowsByModule(QTrading::Log::LogModule::MarketEvent).empty());
    EXPECT_TRUE(FilterRowsByModule(QTrading::Log::LogModule::FundingEvent).empty());
    EXPECT_TRUE(FilterRowsByModule(QTrading::Log::LogModule::AccountEvent).empty());
    EXPECT_TRUE(FilterRowsByModule(QTrading::Log::LogModule::PositionEvent).empty());
    EXPECT_TRUE(FilterRowsByModule(QTrading::Log::LogModule::OrderEvent).empty());
}

TEST_F(BinanceExchangeLogFixture, ReducedObservabilityLiquidationStepEmitsStatusAndMarketWithoutSyntheticFillContractInCurrentKernel)
{
    WriteCsv("liq_trade.csv", {
        {      0, 100,100,100,100,10000, 30000,10000,1,0,0 }
    });
    WriteCsv("liq_mark.csv", {
        {      0, 10,10,10,10,10000, 30000,10000,1,0,0 }
    });

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 1000.0;
    BinanceExchange exchange(
        { { "BTCUSDT",
            (tmp_dir / "liq_trade.csv").string(),
            std::nullopt,
            std::optional<std::string>((tmp_dir / "liq_mark.csv").string()) } },
        logger,
        init,
        7400u);

    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 500.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, 1.0, QTrading::Dto::Trading::OrderSide::Buy));
    ASSERT_TRUE(exchange.step());
    ASSERT_TRUE(exchange.get_market_channel()->Receive().has_value());
    StopLogger();

    const auto account_rows = FilterRowsByModule(QTrading::Log::LogModule::Account);
    const auto market_rows = FilterRowsByModule(QTrading::Log::LogModule::MarketEvent);
    ASSERT_FALSE(account_rows.empty());
    ASSERT_FALSE(market_rows.empty());

    EXPECT_TRUE(FilterRowsByModule(QTrading::Log::LogModule::FundingEvent).empty());
    EXPECT_TRUE(FilterRowsByModule(QTrading::Log::LogModule::AccountEvent).empty());
    EXPECT_TRUE(FilterRowsByModule(QTrading::Log::LogModule::PositionEvent).empty());
    EXPECT_TRUE(FilterRowsByModule(QTrading::Log::LogModule::OrderEvent).empty());
}

} // namespace
