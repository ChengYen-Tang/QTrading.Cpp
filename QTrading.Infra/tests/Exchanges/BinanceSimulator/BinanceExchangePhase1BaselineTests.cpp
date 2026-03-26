#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
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

// Baseline reference: BinanceExchangeFixture.DomainFacadeRoutesByInstrumentType
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

// Baseline source: BinanceExchangeTests.PushOnlyOnChange (trimmed to minimal no-fill contract).
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

// Baseline source: AccountTests.UpdatePositionsPartialFillSameOrder (adapted to exchange step flow).
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

// Baseline source: cases A05/T14 minimal subset (spot market buy with quote-fee settlement).
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

// Baseline source: BinanceExchangeTests.SnapshotConsistent/DomainFacadeRoutesByInstrumentType (perp fill path).
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

} // namespace
