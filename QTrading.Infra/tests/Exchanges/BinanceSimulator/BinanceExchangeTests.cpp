#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <optional>
#include <type_traits>
#include <limits>
#include <cstdlib>

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

using namespace QTrading::Infra::Exchanges::BinanceSim;
using namespace QTrading::Dto::Market::Binance;
using namespace QTrading::Utils::Queue;
namespace fs = std::filesystem;

static_assert(!std::is_move_constructible_v<BinanceExchange>);
static_assert(!std::is_move_assignable_v<BinanceExchange>);

/// @brief Mock logger that discards all consumed rows.
class MockLogger : public QTrading::Log::Logger {
    public:
        /// @brief Construct with output directory path.
        MockLogger(const std::string &dir) : QTrading::Log::Logger(dir) {}

    protected:
        /// @brief No-op consume to disable file output.
        void Consume() override {}
};

namespace {

void set_env_var(const char* key, const char* value)
{
#ifdef _WIN32
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
}

void unset_env_var(const char* key)
{
#ifdef _WIN32
    _putenv_s(key, "");
#else
    unsetenv(key);
#endif
}

class ScopedEnvVar {
public:
    ScopedEnvVar(const char* key, const char* value)
        : key_(key)
    {
        set_env_var(key_.c_str(), value);
    }

    ~ScopedEnvVar()
    {
        unset_env_var(key_.c_str());
    }

private:
    std::string key_;
};

} // namespace

/// @brief Test fixture - provides tmpDir + helper to generate minimal CSV files.
class BinanceExchangeFixture : public ::testing::Test {
protected:
    fs::path tmpDir;          // .../<gtest name>/ directory
    std::shared_ptr<QTrading::Log::Logger> logger;

    /// @brief Write a minimal Binance 1-minute CSV.
    /// @param fileName     Name of CSV file to create under tmpDir.
    /// @param rows         Vector of tuples: 
    ///                     (openTime, open, high, low, close, volume,
    ///                      closeTime, quoteVol, tradeCnt, takerBB, takerBQ)
    void writeCsv(const std::string& fileName,
        const std::vector<std::tuple<
        uint64_t, double, double, double, double, double,
        uint64_t, double, int, double, double>>&rows)
    {
        std::ofstream f(tmpDir / fileName, std::ios::trunc);
        f << "openTime,open,high,low,close,volume,"
            "closeTime,quoteVol,tradeCnt,takerBB,takerBQ\n";
        for (auto& r : rows)
            f << std::get<0>(r) << ','  // openTime
            << std::get<1>(r) << ','  // open
            << std::get<2>(r) << ','  // high
            << std::get<3>(r) << ','  // low
            << std::get<4>(r) << ','  // close
            << std::get<5>(r) << ','  // volume
            << std::get<6>(r) << ','  // closeTime
            << std::get<7>(r) << ','  // quoteVol
            << std::get<8>(r) << ','  // tradeCnt
            << std::get<9>(r) << ','  // takerBB
            << std::get<10>(r) << '\n';// takerBQ
    }

    /// @brief Write a compact 6-column kline CSV.
    void writeCompactCsv(const std::string& fileName,
        const std::vector<std::tuple<uint64_t, double, double, double, double, uint64_t>>& rows)
    {
        std::ofstream f(tmpDir / fileName, std::ios::trunc);
        f << "openTime,open,high,low,close,closeTime\n";
        for (const auto& r : rows) {
            f << std::get<0>(r) << ','
              << std::get<1>(r) << ','
              << std::get<2>(r) << ','
              << std::get<3>(r) << ','
              << std::get<4>(r) << ','
              << std::get<5>(r) << '\n';
        }
    }

    /// @brief Write a minimal funding CSV (FundingTime,Rate,MarkPrice).
    void writeFundingCsv(const std::string& fileName,
        const std::vector<std::tuple<uint64_t, double, std::optional<double>>>& rows)
    {
        std::ofstream f(tmpDir / fileName, std::ios::trunc);
        f << "FundingTime,Rate,MarkPrice\n";
        for (const auto& r : rows) {
            f << std::get<0>(r) << ','  // FundingTime
              << std::get<1>(r) << ','; // Rate
            if (std::get<2>(r).has_value()) {
                f << *std::get<2>(r);
            }
            f << '\n';
        }
    }

    /// @brief Creates tmpDir and starts the mock logger.
    void SetUp() override
    {
        // Each test gets its own directory, eg. "tmp/BinanceExchangeFixture_StepOrdering"
        tmpDir = fs::temp_directory_path() /
            (std::string("QTradingTest_") + ::testing::UnitTest::GetInstance()
                ->current_test_info()->name());
        fs::create_directories(tmpDir);
        logger = std::make_shared<MockLogger>(tmpDir.string());
        logger->Start();
    }

    /// @brief Stops the logger and cleans up tmpDir.
    void TearDown() override
    {
		logger->Stop();            // stop logger thread
        fs::remove_all(tmpDir);     // clean everything we created
    }
};

/// @brief Test syncing of multiple symbols with missing-data holes (std::nullopt).
TEST_F(BinanceExchangeFixture, SymbolsSynchronisedWithHoles)
{
    /* Prepare data:
     *   BTC rows at t=0 ms & 60 000 ms
     *   ETH rows at t=30 000 ms & 60 000 ms
     */
    writeCsv("btc.csv", {
        {      0, 1,1,1,1,100, 30000,100,1,0,0 },
        {  60000, 2,2,2,2,200, 90000,200,1,0,0 }
        });
    writeCsv("eth.csv", {
        {  30000,10,10,10,10, 50, 60000, 50,1,0,0 },
        {  60000,20,20,20,20, 70, 90000, 70,1,0,0 }
        });

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()},
                        {"ETHUSDT",(tmpDir / "eth.csv").string()} }, logger);
    auto mCh = ex.get_market_channel();

    // ---------- step #1  (t = 0) ----------
    ASSERT_TRUE(ex.step());
    auto dto1 = mCh->Receive();
    ASSERT_TRUE(dto1.has_value());
    EXPECT_EQ(dto1->get()->Timestamp, 0u);
    ASSERT_TRUE(dto1->get()->symbols);
    const auto symbols = dto1->get()->symbols;
    auto find_id = [&](const std::string& sym) -> std::size_t {
        for (std::size_t i = 0; i < symbols->size(); ++i) {
            if ((*symbols)[i] == sym) {
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

    // ---------- step #2  (t = 30 000) -----
    ASSERT_TRUE(ex.step());
    auto dto2 = mCh->Receive();
    EXPECT_EQ(dto2->get()->Timestamp, 30000u);
    EXPECT_FALSE(dto2->get()->trade_klines_by_id[btc_id].has_value());
    EXPECT_TRUE(dto2->get()->trade_klines_by_id[eth_id].has_value());

    // ---------- step #3  (t = 60 000) -----
    ASSERT_TRUE(ex.step());
    auto dto3 = mCh->Receive();
    EXPECT_EQ(dto3->get()->Timestamp, 60000u);
    EXPECT_TRUE(dto3->get()->trade_klines_by_id[btc_id].has_value());
    EXPECT_TRUE(dto3->get()->trade_klines_by_id[eth_id].has_value());

    // ---------- step #4  (EOF) ------------
    EXPECT_FALSE(ex.step());          // nothing left
}

TEST_F(BinanceExchangeFixture, ReplayWindowFiltersKlineRangeByTimestampEnv)
{
    writeCsv("btc.csv", {
        {      0, 1,1,1,1,100,  30000,100,1,0,0 },
        {  60000, 2,2,2,2,200,  90000,200,1,0,0 },
        { 120000, 3,3,3,3,300, 150000,300,1,0,0 }
        });

    // Keep only the middle bar.
    ScopedEnvVar start_guard("QTR_SIM_START_TS_MS", "60000");
    ScopedEnvVar end_guard("QTR_SIM_END_TS_MS", "60000");

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger);
    auto mCh = ex.get_market_channel();

    ASSERT_TRUE(ex.step());
    auto dto = mCh->Receive();
    ASSERT_TRUE(dto.has_value());
    EXPECT_EQ(dto->get()->Timestamp, 60000u);

    EXPECT_FALSE(ex.step());
}

TEST_F(BinanceExchangeFixture, ReplayWindowFiltersKlineRangeByDateEnv)
{
    // 2023-02-03 00:00:00 UTC
    constexpr uint64_t t0 = 1675382400000ULL;
    // 2023-02-03 12:00:00 UTC
    constexpr uint64_t t1 = 1675425600000ULL;
    // 2023-02-04 00:00:00 UTC
    constexpr uint64_t t2 = 1675468800000ULL;

    writeCsv("btc.csv", {
        { t0, 1,1,1,1,100, t0 + 30000,100,1,0,0 },
        { t1, 2,2,2,2,200, t1 + 30000,200,1,0,0 },
        { t2, 3,3,3,3,300, t2 + 30000,300,1,0,0 }
        });

    ScopedEnvVar start_guard("QTR_SIM_START_DATE", "2023-02-03");
    ScopedEnvVar end_guard("QTR_SIM_END_DATE", "2023-02-03");

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger);
    auto mCh = ex.get_market_channel();

    ASSERT_TRUE(ex.step());
    auto dto0 = mCh->Receive();
    ASSERT_TRUE(dto0.has_value());
    EXPECT_EQ(dto0->get()->Timestamp, t0);

    ASSERT_TRUE(ex.step());
    auto dto1 = mCh->Receive();
    ASSERT_TRUE(dto1.has_value());
    EXPECT_EQ(dto1->get()->Timestamp, t1);

    EXPECT_FALSE(ex.step());
}

/// @brief Test that position and order channels emit only on state change.
TEST_F(BinanceExchangeFixture, PushOnlyOnChange)
{
    writeCsv("btc.csv", {
        {      0,1,1,1,1,100, 30000,100,1,0,0 },
        {  60000,2,2,2,2,100, 90000,100,1,0,0 },
        {  90000,1,1,1,1,100, 90000,100,1,0,0 }
        });

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger, /*balance*/ 1000.0);
    auto mCh = ex.get_market_channel();
    auto oCh = ex.get_order_channel();
    auto pCh = ex.get_position_channel();

    /* ---- step 1 : first market bar, no state yet ---- */
    ex.step();     mCh->Receive();             // drain
    EXPECT_FALSE(oCh->TryReceive().has_value());
    EXPECT_FALSE(pCh->TryReceive().has_value());

    using QTrading::Dto::Trading::OrderSide;
    /* ---- create an order -> should appear next step -- */
    ASSERT_TRUE(ex.perp.place_order("BTCUSDT", 1.0, 1.0, OrderSide::Buy));

    ex.step();     mCh->Receive();
    auto ordSnap = oCh->Receive();
    EXPECT_EQ(ordSnap->size(), 1u);
    EXPECT_FALSE(pCh->TryReceive().has_value());

    /* ---- after fill, order disappears, position appears --- */
    ex.step();     mCh->Receive();
    auto posSnap = pCh->Receive();
    EXPECT_EQ(posSnap->size(), 1u);

    auto ordSnap2 = oCh->Receive();
    EXPECT_TRUE(ordSnap2->empty());

    ///* ---- EOF : channels already closed, no new pushes ---- */
    ex.step();
    EXPECT_FALSE(oCh->TryReceive().has_value());
    EXPECT_FALSE(pCh->TryReceive().has_value());
}

TEST_F(BinanceExchangeFixture, OrderLatencyBarsDelaysPlacementUntilFutureStep)
{
    writeCsv("btc.csv", {
        {      0,1,1,1,1,100, 30000,100,1,0,0 },
        {  60000,1,1,1,1,100, 90000,100,1,0,0 }
        });

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger, /*balance*/ 1000.0);
    ex.set_order_latency_bars(1);
    auto mCh = ex.get_market_channel();

    // Step 1: process first bar.
    ASSERT_TRUE(ex.step());
    (void)mCh->Receive();

    using QTrading::Dto::Trading::OrderSide;
    ASSERT_TRUE(ex.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
    // Order is queued, not yet in account book.
    EXPECT_TRUE(ex.get_all_open_orders().empty());
    EXPECT_TRUE(ex.get_all_positions().empty());

    // Step 2: queued order becomes active and can fill on this bar.
    ASSERT_TRUE(ex.step());
    (void)mCh->Receive();
    EXPECT_EQ(ex.get_all_positions().size(), 1u);
}

TEST_F(BinanceExchangeFixture, OrderLatencyBarsPublishesPendingThenAcceptedAsyncAck)
{
    writeCsv("btc.csv", {
        {      0,1,1,1,1,100, 30000,100,1,0,0 },
        {  60000,1,1,1,1,100, 90000,100,1,0,0 }
        });

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger, /*balance*/ 1000.0);
    ex.set_order_latency_bars(1);
    auto mCh = ex.get_market_channel();

    ASSERT_TRUE(ex.step());
    (void)mCh->Receive();

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    ASSERT_TRUE(ex.perp.place_order(
        "BTCUSDT",
        1.0,
        0.0,
        OrderSide::Buy,
        PositionSide::Both,
        false,
        "cid-lat-1",
        Account::SelfTradePreventionMode::ExpireMaker));

    auto acks = ex.drain_async_order_acks();
    ASSERT_EQ(acks.size(), 1u);
    EXPECT_EQ(acks[0].status, BinanceExchange::AsyncOrderAck::Status::Pending);
    EXPECT_EQ(acks[0].instrument_type, QTrading::Dto::Trading::InstrumentType::Perp);
    EXPECT_EQ(acks[0].symbol, "BTCUSDT");
    EXPECT_EQ(acks[0].submitted_step, 1u);
    EXPECT_EQ(acks[0].due_step, 2u);
    EXPECT_EQ(acks[0].resolved_step, 0u);
    EXPECT_EQ(acks[0].reject_code, Account::OrderRejectInfo::Code::None);
    EXPECT_TRUE(acks[0].reject_message.empty());
    EXPECT_EQ(acks[0].client_order_id, "cid-lat-1");
    EXPECT_EQ(acks[0].stp_mode, Account::SelfTradePreventionMode::ExpireMaker);
    EXPECT_EQ(acks[0].binance_error_code, 0);
    EXPECT_TRUE(acks[0].binance_error_message.empty());
    const uint64_t req_id = acks[0].request_id;

    ASSERT_TRUE(ex.step());
    (void)mCh->Receive();

    acks = ex.drain_async_order_acks();
    ASSERT_EQ(acks.size(), 1u);
    EXPECT_EQ(acks[0].request_id, req_id);
    EXPECT_EQ(acks[0].status, BinanceExchange::AsyncOrderAck::Status::Accepted);
    EXPECT_EQ(acks[0].resolved_step, 2u);
    EXPECT_EQ(acks[0].reject_code, Account::OrderRejectInfo::Code::None);
    EXPECT_TRUE(acks[0].reject_message.empty());
    EXPECT_EQ(acks[0].client_order_id, "cid-lat-1");
    EXPECT_EQ(acks[0].stp_mode, Account::SelfTradePreventionMode::ExpireMaker);
    EXPECT_EQ(acks[0].binance_error_code, 0);
    EXPECT_TRUE(acks[0].binance_error_message.empty());
    EXPECT_EQ(ex.get_all_positions().size(), 1u);
}

TEST_F(BinanceExchangeFixture, OrderLatencyBarsPublishesPendingThenRejectedAsyncAck)
{
    writeCsv("btc.csv", {
        {      0,100,100,100,100,100, 30000,100,1,0,0 },
        {  60000,100,100,100,100,100, 90000,100,1,0,0 }
        });

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger, /*balance*/ 1000.0);
    ex.set_order_latency_bars(1);
    auto mCh = ex.get_market_channel();

    ASSERT_TRUE(ex.step());
    (void)mCh->Receive();

    using QTrading::Dto::Trading::OrderSide;
    ASSERT_TRUE(ex.spot.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Sell));

    auto acks = ex.drain_async_order_acks();
    ASSERT_EQ(acks.size(), 1u);
    EXPECT_EQ(acks[0].status, BinanceExchange::AsyncOrderAck::Status::Pending);
    EXPECT_EQ(acks[0].instrument_type, QTrading::Dto::Trading::InstrumentType::Spot);
    EXPECT_EQ(acks[0].reject_code, Account::OrderRejectInfo::Code::None);
    EXPECT_TRUE(acks[0].reject_message.empty());
    EXPECT_EQ(acks[0].binance_error_code, 0);
    EXPECT_TRUE(acks[0].binance_error_message.empty());
    const uint64_t req_id = acks[0].request_id;

    ASSERT_TRUE(ex.step());
    (void)mCh->Receive();

    acks = ex.drain_async_order_acks();
    ASSERT_EQ(acks.size(), 1u);
    EXPECT_EQ(acks[0].request_id, req_id);
    EXPECT_EQ(acks[0].status, BinanceExchange::AsyncOrderAck::Status::Rejected);
    EXPECT_EQ(acks[0].resolved_step, 2u);
    EXPECT_EQ(acks[0].reject_code, Account::OrderRejectInfo::Code::SpotNoInventory);
    EXPECT_FALSE(acks[0].reject_message.empty());
    EXPECT_EQ(acks[0].binance_error_code, -2010);
    EXPECT_FALSE(acks[0].binance_error_message.empty());
    EXPECT_TRUE(ex.get_all_positions().empty());
    EXPECT_TRUE(ex.get_all_open_orders().empty());
}

/// @brief Test that snapshot getters return consistent data before/after fills.
TEST_F(BinanceExchangeFixture, SnapshotConsistent)
{
    writeCsv("btc.csv", {
        {0,1,1,1,1,10, 30000,10,1,0,0}
        });

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger, /*balance*/ 500.0);

    // 1) No positions yet
    EXPECT_TRUE(ex.get_all_positions().empty());

    using QTrading::Dto::Trading::OrderSide;
    // 2) Place market order 0.5 BTC long
    ASSERT_TRUE(ex.perp.place_order("BTCUSDT", 0.5, OrderSide::Buy));
    ex.step();

    EXPECT_EQ(ex.get_all_positions().size(), 1u);
    EXPECT_TRUE(ex.get_all_open_orders().empty());
}

TEST_F(BinanceExchangeFixture, ConstructWithAccountInitConfig)
{
    writeCsv("btc.csv", {
        {0,1,1,1,1,10, 30000,10,1,0,0}
        });

    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 100.0;
    cfg.perp_initial_wallet = 900.0;
    cfg.vip_level = 0;

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger, cfg);
    auto mCh = ex.get_market_channel();
    ASSERT_TRUE(ex.step());
    auto dto = mCh->Receive();
    ASSERT_TRUE(dto.has_value());
    EXPECT_EQ(dto->get()->Timestamp, 0u);
}

TEST_F(BinanceExchangeFixture, DomainFacadeRoutesByInstrumentType)
{
    writeCsv("btc.csv", {
        {0,100,100,100,100,1000, 30000,100,1,0,0}
        });
    writeCsv("eth.csv", {
        {0,100,100,100,100,1000, 30000,100,1,0,0}
        });

    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 1000.0;

    BinanceExchange ex(
        { {"BTCUSDT",(tmpDir / "btc.csv").string()},
          {"ETHUSDT",(tmpDir / "eth.csv").string()} },
        logger,
        cfg);

    using QTrading::Dto::Trading::OrderSide;
    ASSERT_TRUE(ex.spot.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy));
    ASSERT_TRUE(ex.perp.place_order("ETHUSDT", 1.0, 100.0, OrderSide::Sell));

    ASSERT_TRUE(ex.step());
    const auto& pos = ex.get_all_positions();
    ASSERT_EQ(pos.size(), 2u);
}

TEST_F(BinanceExchangeFixture, AccountFacadeTransfersRespectAvailability)
{
    writeCsv("btc.csv", {
        {0,100,100,100,100,1000, 30000,100,1,0,0}
        });

    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 500.0;
    cfg.perp_initial_wallet = 500.0;

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger, cfg);
    EXPECT_TRUE(ex.account.transfer_spot_to_perp(100.0));
    EXPECT_TRUE(ex.account.transfer_perp_to_spot(200.0));

    using QTrading::Dto::Trading::OrderSide;
    ASSERT_TRUE(ex.spot.place_order("BTCUSDT", 3.0, 100.0, OrderSide::Buy));
    EXPECT_FALSE(ex.account.transfer_spot_to_perp(300.0));
}

TEST_F(BinanceExchangeFixture, CoreModeDefaultsToLegacyOnly)
{
    writeCsv("btc.csv", {
        {0,100,100,100,100,1000, 30000,100,1,0,0}
        });

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger, /*balance*/ 1000.0);
    EXPECT_EQ(ex.core_mode(), BinanceExchange::CoreMode::LegacyOnly);
    EXPECT_FALSE(ex.consume_last_compare_diagnostic().has_value());
}

TEST_F(BinanceExchangeFixture, CoreModePolicyClassifiesAndNamesModes)
{
    using CoreMode = BinanceExchange::CoreMode;
    using Policy = BinanceExchange::CoreModePolicy;

    EXPECT_TRUE(Policy::is_legacy_only(CoreMode::LegacyOnly));
    EXPECT_FALSE(Policy::allows_compare_diagnostics(CoreMode::LegacyOnly));
    EXPECT_STREQ(Policy::name(CoreMode::LegacyOnly), "LegacyOnly");

    EXPECT_TRUE(Policy::is_shadow_compare(CoreMode::NewCoreShadow));
    EXPECT_TRUE(Policy::allows_compare_diagnostics(CoreMode::NewCoreShadow));
    EXPECT_STREQ(Policy::name(CoreMode::NewCoreShadow), "NewCoreShadow");

    EXPECT_TRUE(Policy::is_new_core_primary(CoreMode::NewCorePrimary));
    EXPECT_TRUE(Policy::allows_compare_diagnostics(CoreMode::NewCorePrimary));
    EXPECT_STREQ(Policy::name(CoreMode::NewCorePrimary), "NewCorePrimary");

    EXPECT_EQ(Policy::normalize(static_cast<CoreMode>(99)), CoreMode::LegacyOnly);
    EXPECT_STREQ(Policy::name(static_cast<CoreMode>(99)), "LegacyOnly");
}

TEST_F(BinanceExchangeFixture, InvalidCoreModeFallsBackToLegacyOnly)
{
    writeCsv("btc.csv", {
        {0,100,100,100,100,1000, 30000,100,1,0,0}
        });

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger, /*balance*/ 1000.0);
    ex.set_core_mode(static_cast<BinanceExchange::CoreMode>(99));
    EXPECT_EQ(ex.core_mode(), BinanceExchange::CoreMode::LegacyOnly);
}

TEST_F(BinanceExchangeFixture, NewCoreShadowKeepsLegacyPathAndRecordsDiagnostic)
{
    writeCsv("btc.csv", {
        {0,100,100,100,100,1000, 30000,100,1,0,0}
        });

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger, /*balance*/ 1000.0);
    ex.set_core_mode(BinanceExchange::CoreMode::NewCoreShadow);

    ASSERT_TRUE(ex.step());
    auto diag = ex.consume_last_compare_diagnostic();
    ASSERT_TRUE(diag.has_value());
    EXPECT_EQ(diag->mode, BinanceExchange::CoreMode::NewCoreShadow);
    EXPECT_FALSE(diag->compared);
    EXPECT_TRUE(diag->matched);
    EXPECT_FALSE(diag->reason.empty());
    EXPECT_FALSE(ex.consume_last_compare_diagnostic().has_value());
}

TEST_F(BinanceExchangeFixture, NewCorePrimaryFallsBackToLegacyPath)
{
    writeCsv("btc.csv", {
        {0,100,100,100,100,1000, 30000,100,1,0,0}
        });

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger, /*balance*/ 1000.0);
    ex.set_core_mode(BinanceExchange::CoreMode::NewCorePrimary);

    using QTrading::Dto::Trading::OrderSide;
    ASSERT_TRUE(ex.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
    ASSERT_TRUE(ex.step());

    const auto& positions = ex.get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    auto diag = ex.consume_last_compare_diagnostic();
    ASSERT_TRUE(diag.has_value());
    EXPECT_EQ(diag->mode, BinanceExchange::CoreMode::NewCorePrimary);
    EXPECT_FALSE(diag->compared);
    EXPECT_TRUE(diag->matched);
    EXPECT_FALSE(diag->reason.empty());
}

TEST_F(BinanceExchangeFixture, NewCorePrimaryBridgeRoutesToV2WhenAvailable)
{
    writeCsv("btc.csv", {
        {0,100,100,100,100,1000, 30000,100,1,0,0}
        });

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger, /*balance*/ 1000.0);
    ex.set_core_mode(BinanceExchange::CoreMode::NewCorePrimary);

    auto bridge_diag = ex.consume_last_account_facade_bridge_diagnostic();
    ASSERT_TRUE(bridge_diag.has_value());
    EXPECT_EQ(bridge_diag->mode, BinanceExchange::CoreMode::NewCorePrimary);
    EXPECT_TRUE(bridge_diag->has_v2);
    EXPECT_TRUE(bridge_diag->routed_to_v2);
    EXPECT_TRUE(bridge_diag->production_default_legacy_only);
    EXPECT_FALSE(bridge_diag->reason.empty());

    using QTrading::Dto::Trading::OrderSide;
    ASSERT_TRUE(ex.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
    ASSERT_TRUE(ex.step());
    EXPECT_EQ(ex.get_all_positions().size(), 1u);
}

TEST_F(BinanceExchangeFixture, NewCorePrimaryBridgeFallsBackWhenV2Unavailable)
{
    writeCsv("btc.csv", {
        {0,100,100,100,100,1000, 30000,100,1,0,0}
        });

    auto legacy_account = std::make_shared<Account>(1000.0, 0);
    BinanceExchange ex(
        { {"BTCUSDT",(tmpDir / "btc.csv").string()} },
        logger,
        legacy_account);

    ex.set_core_mode(BinanceExchange::CoreMode::NewCorePrimary);
    auto bridge_diag = ex.consume_last_account_facade_bridge_diagnostic();
    ASSERT_TRUE(bridge_diag.has_value());
    EXPECT_EQ(bridge_diag->mode, BinanceExchange::CoreMode::NewCorePrimary);
    EXPECT_FALSE(bridge_diag->has_v2);
    EXPECT_FALSE(bridge_diag->routed_to_v2);
    EXPECT_TRUE(bridge_diag->production_default_legacy_only);
    EXPECT_FALSE(bridge_diag->reason.empty());

    using QTrading::Dto::Trading::OrderSide;
    ASSERT_TRUE(ex.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
    ASSERT_TRUE(ex.step());
    EXPECT_EQ(ex.get_all_positions().size(), 1u);
}

TEST_F(BinanceExchangeFixture, FacadeContractLegacyAndV2PrimaryKeepInputReturnAndSnapshotConsistent)
{
    writeCsv("btc.csv", {
        {      0,100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000,105,105,105,105,1000, 90000,100,1,0,0 }
        });
    writeFundingCsv("btc_funding.csv", {
        {  60000, 0.001, 100.0 }
        });

    struct RunResult {
        bool place_ok{ false };
        bool invalid_ok{ false };
        std::vector<QTrading::dto::Position> positions{};
        std::vector<QTrading::dto::Order> orders{};
        QTrading::Dto::Account::BalanceSnapshot perp{};
        QTrading::Dto::Account::BalanceSnapshot spot{};
        double total_cash{ 0.0 };
        BinanceExchange::StatusSnapshot snapshot{};
        std::optional<BinanceExchange::AccountFacadeBridgeDiagnostic> bridge_diag{};
    };

    auto run_case = [&](bool with_v2, BinanceExchange::CoreMode mode) -> RunResult {
        Account::AccountInitConfig cfg{};
        cfg.spot_initial_cash = 500.0;
        cfg.perp_initial_wallet = 1000.0;
        cfg.vip_level = 0;

        auto account = std::make_shared<Account>(cfg);
        std::shared_ptr<AccountCoreV2> v2{};
        if (with_v2) {
            v2 = std::make_shared<AccountCoreV2>(cfg);
        }

        BinanceExchange ex(
            { BinanceExchange::SymbolDataset{
                "BTCUSDT",
                (tmpDir / "btc.csv").string(),
                std::optional<std::string>((tmpDir / "btc_funding.csv").string()),
                std::nullopt,
                std::nullopt,
                QTrading::Dto::Trading::InstrumentType::Perp} },
            logger,
            account,
            v2,
            112233ull);
        ex.set_core_mode(mode);

        RunResult out{};
        out.bridge_diag = ex.consume_last_account_facade_bridge_diagnostic();
        const auto market_channel = ex.get_market_channel();

        using QTrading::Dto::Trading::OrderSide;
        EXPECT_NO_THROW(out.place_ok = ex.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        EXPECT_NO_THROW(out.invalid_ok = ex.perp.place_order("BTCUSDT", -1.0, OrderSide::Buy));

        const bool step1 = ex.step();
        EXPECT_TRUE(step1);
        if (step1) {
            EXPECT_TRUE(market_channel->Receive().has_value());
        }
        const bool step2 = ex.step();
        EXPECT_TRUE(step2);
        if (step2) {
            EXPECT_TRUE(market_channel->Receive().has_value());
        }

        out.positions = ex.get_all_positions();
        out.orders = ex.get_all_open_orders();
        out.perp = ex.get_perp_balance();
        out.spot = ex.get_spot_balance();
        out.total_cash = ex.get_total_cash_balance();
        ex.FillStatusSnapshot(out.snapshot);
        return out;
    };

    const auto legacy = run_case(false, BinanceExchange::CoreMode::LegacyOnly);
    const auto v2_primary = run_case(true, BinanceExchange::CoreMode::NewCorePrimary);

    EXPECT_TRUE(legacy.place_ok);
    EXPECT_TRUE(v2_primary.place_ok);
    EXPECT_FALSE(legacy.invalid_ok);
    EXPECT_FALSE(v2_primary.invalid_ok);

    EXPECT_TRUE(BinanceExchange::vec_equal(legacy.positions, v2_primary.positions));
    EXPECT_TRUE(BinanceExchange::vec_equal(legacy.orders, v2_primary.orders));
    EXPECT_NEAR(legacy.perp.WalletBalance, v2_primary.perp.WalletBalance, 1e-9);
    EXPECT_NEAR(legacy.spot.WalletBalance, v2_primary.spot.WalletBalance, 1e-9);
    EXPECT_NEAR(legacy.total_cash, v2_primary.total_cash, 1e-9);
    EXPECT_NEAR(legacy.snapshot.total_ledger_value, v2_primary.snapshot.total_ledger_value, 1e-9);
    EXPECT_EQ(legacy.snapshot.ts_exchange, v2_primary.snapshot.ts_exchange);

    ASSERT_TRUE(v2_primary.bridge_diag.has_value());
    EXPECT_EQ(v2_primary.bridge_diag->mode, BinanceExchange::CoreMode::NewCorePrimary);
    EXPECT_TRUE(v2_primary.bridge_diag->has_v2);
    EXPECT_TRUE(v2_primary.bridge_diag->routed_to_v2);
}

TEST_F(BinanceExchangeFixture, FacadeContractFallbackKeepsLegacyBaselineWhenV2Unavailable)
{
    writeCsv("btc.csv", {
        {      0,100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000,90,90,90,90,1000, 90000,100,1,0,0 }
        });
    writeFundingCsv("btc_funding.csv", {
        {  60000, -0.001, 100.0 }
        });

    auto run_case = [&](BinanceExchange::CoreMode mode)
        -> std::tuple<
            std::vector<QTrading::dto::Position>,
            std::vector<QTrading::dto::Order>,
            double,
            BinanceExchange::StatusSnapshot,
            std::optional<BinanceExchange::AccountFacadeBridgeDiagnostic>> {
        auto account = std::make_shared<Account>(1000.0, 0);
        BinanceExchange ex(
            { BinanceExchange::SymbolDataset{
                "BTCUSDT",
                (tmpDir / "btc.csv").string(),
                std::optional<std::string>((tmpDir / "btc_funding.csv").string()),
                std::nullopt,
                std::nullopt,
                QTrading::Dto::Trading::InstrumentType::Perp} },
            logger,
            account);
        ex.set_core_mode(mode);

        const auto bridge_diag = ex.consume_last_account_facade_bridge_diagnostic();
        using QTrading::Dto::Trading::OrderSide;
        const bool placed = ex.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy);
        EXPECT_TRUE(placed);

        const auto market_channel = ex.get_market_channel();
        const bool step1 = ex.step();
        EXPECT_TRUE(step1);
        if (step1) {
            EXPECT_TRUE(market_channel->Receive().has_value());
        }
        const bool step2 = ex.step();
        EXPECT_TRUE(step2);
        if (step2) {
            EXPECT_TRUE(market_channel->Receive().has_value());
        }

        BinanceExchange::StatusSnapshot snapshot{};
        ex.FillStatusSnapshot(snapshot);
        return std::make_tuple(
            ex.get_all_positions(),
            ex.get_all_open_orders(),
            ex.get_total_cash_balance(),
            snapshot,
            bridge_diag);
    };

    const auto legacy = run_case(BinanceExchange::CoreMode::LegacyOnly);
    const auto fallback = run_case(BinanceExchange::CoreMode::NewCorePrimary);

    EXPECT_TRUE(BinanceExchange::vec_equal(std::get<0>(legacy), std::get<0>(fallback)));
    EXPECT_TRUE(BinanceExchange::vec_equal(std::get<1>(legacy), std::get<1>(fallback)));
    EXPECT_NEAR(std::get<2>(legacy), std::get<2>(fallback), 1e-9);
    EXPECT_NEAR(std::get<3>(legacy).total_ledger_value, std::get<3>(fallback).total_ledger_value, 1e-9);
    EXPECT_EQ(std::get<3>(legacy).ts_exchange, std::get<3>(fallback).ts_exchange);

    ASSERT_TRUE(std::get<4>(fallback).has_value());
    EXPECT_FALSE(std::get<4>(fallback)->has_v2);
    EXPECT_FALSE(std::get<4>(fallback)->routed_to_v2);
}

TEST_F(BinanceExchangeFixture, EventPublishModeDefaultsToLegacyDirect)
{
    writeCsv("btc.csv", {
        {0,100,100,100,100,1000, 30000,100,1,0,0}
        });

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger, /*balance*/ 1000.0);
    EXPECT_EQ(ex.event_publish_mode(), BinanceExchange::EventPublishMode::LegacyDirect);
    EXPECT_FALSE(ex.consume_last_event_publish_diagnostic().has_value());
}

TEST_F(BinanceExchangeFixture, InvalidEventPublishModeFallsBackToLegacyDirect)
{
    writeCsv("btc.csv", {
        {0,100,100,100,100,1000, 30000,100,1,0,0}
        });

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger, /*balance*/ 1000.0);
    ex.set_event_publish_mode(static_cast<BinanceExchange::EventPublishMode>(99));
    EXPECT_EQ(ex.event_publish_mode(), BinanceExchange::EventPublishMode::LegacyDirect);
}

TEST_F(BinanceExchangeFixture, EventPublishDualCompareRecordsDiagnostic)
{
    writeCsv("btc.csv", {
        {0,100,100,100,100,1000, 30000,100,1,0,0}
        });

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger, /*balance*/ 1000.0);
    ex.set_event_publish_mode(BinanceExchange::EventPublishMode::DualPublishCompare);

    ASSERT_TRUE(ex.step());
    auto diag = ex.consume_last_event_publish_diagnostic();
    ASSERT_TRUE(diag.has_value());
    EXPECT_EQ(diag->mode, BinanceExchange::EventPublishMode::DualPublishCompare);
    EXPECT_TRUE(diag->compared);
    EXPECT_TRUE(diag->matched);
    EXPECT_TRUE(diag->reason.empty());
}

TEST_F(BinanceExchangeFixture, DualPublishCompareDeterministicReplayProducesStableDiagnostics)
{
    writeCsv("btc.csv", {
        {     0,100,101,99,100,1000, 30000,100,1,0,0},
        { 60000,101,102,100,101,1100, 90000,110,1,0,0},
        {120000,102,103,101,102,1200,150000,120,1,0,0}
        });

    auto run_once = [&]() {
        BinanceExchange ex(
            { {"BTCUSDT",(tmpDir / "btc.csv").string()} },
            logger,
            /*balance*/ 1000.0,
            /*vip_level*/ 0,
            /*run_id*/ 123456789ull);
        ex.set_event_publish_mode(BinanceExchange::EventPublishMode::DualPublishCompare);

        std::vector<uint64_t> signatures;
        while (ex.step()) {
            auto diag = ex.consume_last_event_publish_diagnostic();
            EXPECT_TRUE(diag.has_value());
            if (!diag.has_value()) {
                break;
            }
            EXPECT_TRUE(diag->compared);
            EXPECT_TRUE(diag->matched);
            EXPECT_TRUE(diag->reason.empty());

            const uint64_t sig =
                diag->legacy.payload_fingerprint ^
                (diag->legacy.market_event_count << 1) ^
                (diag->legacy.funding_event_count << 9) ^
                (diag->legacy.account_snapshot_row_count << 17) ^
                (diag->legacy.position_snapshot_row_count << 25) ^
                (diag->legacy.order_snapshot_row_count << 33);
            signatures.push_back(sig);
        }
        return signatures;
    };

    const auto first = run_once();
    const auto second = run_once();
    EXPECT_EQ(first, second);
}

TEST_F(BinanceExchangeFixture, SideEffectAdaptersCaptureMaterializedSnapshotsAndHooks)
{
    writeCsv("btc.csv", {
        {      0,1,1,1,1,100, 30000,100,1,0,0 },
        {  60000,2,2,2,2,100, 90000,100,1,0,0 }
        });

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger, /*balance*/ 1000.0);
    auto mCh = ex.get_market_channel();
    auto pCh = ex.get_position_channel();
    auto oCh = ex.get_order_channel();

    std::vector<uint64_t> market_timestamps;
    std::vector<size_t> position_sizes;
    std::vector<size_t> order_sizes;
    std::vector<BinanceExchange::SideEffectStepSnapshot> snapshots;

    BinanceExchange::SideEffectAdapterConfig adapters{};
    adapters.market_publisher = [&](MultiKlinePtr dto) {
        EXPECT_TRUE(dto != nullptr);
        if (dto) {
            market_timestamps.push_back(dto->Timestamp);
        }
    };
    adapters.position_publisher = [&](const std::vector<QTrading::dto::Position>& positions) {
        position_sizes.push_back(positions.size());
    };
    adapters.order_publisher = [&](const std::vector<QTrading::dto::Order>& orders) {
        order_sizes.push_back(orders.size());
    };
    adapters.external_hook = [&](const BinanceExchange::SideEffectStepSnapshot& snap) {
        snapshots.push_back(snap);
    };
    ex.set_side_effect_adapters(std::move(adapters));

    ASSERT_TRUE(ex.step());
    EXPECT_FALSE(mCh->TryReceive().has_value());
    EXPECT_FALSE(pCh->TryReceive().has_value());
    EXPECT_FALSE(oCh->TryReceive().has_value());

    using QTrading::Dto::Trading::OrderSide;
    ASSERT_TRUE(ex.perp.place_order("BTCUSDT", 1.0, 1.0, OrderSide::Buy));
    ASSERT_TRUE(ex.step());
    EXPECT_FALSE(mCh->TryReceive().has_value());
    EXPECT_FALSE(pCh->TryReceive().has_value());
    EXPECT_FALSE(oCh->TryReceive().has_value());

    const std::vector<uint64_t> expected_market_ts{ 0u, 60000u };
    EXPECT_EQ(market_timestamps, expected_market_ts);
    EXPECT_TRUE(position_sizes.empty());
    ASSERT_EQ(order_sizes.size(), 1u);
    EXPECT_EQ(order_sizes[0], 1u);

    ASSERT_EQ(snapshots.size(), 2u);
    EXPECT_EQ(snapshots[0].step_seq, 1u);
    EXPECT_EQ(snapshots[0].ts_exchange, 0u);
    EXPECT_TRUE(snapshots[0].market_published);
    EXPECT_FALSE(snapshots[0].position_published);
    EXPECT_FALSE(snapshots[0].order_published);

    EXPECT_EQ(snapshots[1].step_seq, 2u);
    EXPECT_EQ(snapshots[1].ts_exchange, 60000u);
    EXPECT_TRUE(snapshots[1].market_published);
    EXPECT_FALSE(snapshots[1].position_published);
    EXPECT_TRUE(snapshots[1].order_published);
    EXPECT_GE(snapshots[1].state_version, snapshots[0].state_version);
}

TEST_F(BinanceExchangeFixture, ResetSideEffectAdaptersRestoresLegacyChannelPublish)
{
    writeCsv("btc.csv", {
        {      0,1,1,1,1,100, 30000,100,1,0,0 },
        {  60000,2,2,2,2,100, 90000,100,1,0,0 }
        });

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger, /*balance*/ 1000.0);
    auto mCh = ex.get_market_channel();

    BinanceExchange::SideEffectAdapterConfig adapters{};
    adapters.market_publisher = [](MultiKlinePtr) {
        // swallow market publish
    };
    ex.set_side_effect_adapters(std::move(adapters));

    ASSERT_TRUE(ex.step());
    EXPECT_FALSE(mCh->TryReceive().has_value());

    ex.reset_side_effect_adapters();

    ASSERT_TRUE(ex.step());
    auto dto = mCh->Receive();
    ASSERT_TRUE(dto.has_value());
    EXPECT_EQ(dto->get()->Timestamp, 60000u);
}

TEST_F(BinanceExchangeFixture, ForwardingSideEffectAdaptersPreservePublishOrderingAndEventDiagnostics)
{
    writeCsv("btc.csv", {
        {      0,1,1,1,1,100, 30000,100,1,0,0 },
        {  60000,2,2,2,2,100, 90000,100,1,0,0 },
        {  90000,1,1,1,1,100,120000,100,1,0,0 }
        });

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger, /*balance*/ 1000.0);
    auto mCh = ex.get_market_channel();
    auto pCh = ex.get_position_channel();
    auto oCh = ex.get_order_channel();

    std::vector<char> publish_sequence;
    BinanceExchange::SideEffectAdapterConfig adapters{};
    adapters.market_publisher = [&](MultiKlinePtr dto) {
        publish_sequence.push_back('M');
        mCh->Send(dto);
    };
    adapters.position_publisher = [&](const std::vector<QTrading::dto::Position>& positions) {
        publish_sequence.push_back('P');
        pCh->Send(positions);
    };
    adapters.order_publisher = [&](const std::vector<QTrading::dto::Order>& orders) {
        publish_sequence.push_back('O');
        oCh->Send(orders);
    };
    ex.set_side_effect_adapters(std::move(adapters));
    ex.set_event_publish_mode(BinanceExchange::EventPublishMode::DualPublishCompare);

    ASSERT_TRUE(ex.step());
    ASSERT_TRUE(mCh->Receive().has_value());
    auto diag1 = ex.consume_last_event_publish_diagnostic();
    ASSERT_TRUE(diag1.has_value());
    EXPECT_TRUE(diag1->matched);

    using QTrading::Dto::Trading::OrderSide;
    ASSERT_TRUE(ex.perp.place_order("BTCUSDT", 1.0, 1.0, OrderSide::Buy));

    ASSERT_TRUE(ex.step());
    ASSERT_TRUE(mCh->Receive().has_value());
    auto ord1 = oCh->Receive();
    ASSERT_TRUE(ord1.has_value());
    EXPECT_EQ(ord1->size(), 1u);
    auto diag2 = ex.consume_last_event_publish_diagnostic();
    ASSERT_TRUE(diag2.has_value());
    EXPECT_TRUE(diag2->matched);

    ASSERT_TRUE(ex.step());
    ASSERT_TRUE(mCh->Receive().has_value());
    auto pos = pCh->Receive();
    ASSERT_TRUE(pos.has_value());
    EXPECT_EQ(pos->size(), 1u);
    auto ord2 = oCh->Receive();
    ASSERT_TRUE(ord2.has_value());
    EXPECT_TRUE(ord2->empty());
    auto diag3 = ex.consume_last_event_publish_diagnostic();
    ASSERT_TRUE(diag3.has_value());
    EXPECT_TRUE(diag3->matched);

    const std::vector<char> expected_sequence{ 'M', 'M', 'O', 'M', 'P', 'O' };
    EXPECT_EQ(publish_sequence, expected_sequence);
}

TEST_F(BinanceExchangeFixture, FundingAppliedAndDeduped)
{
    writeCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000, 110,110,110,110,1000, 90000,100,1,0,0 },
        { 120000, 120,120,120,120,1000,150000,100,1,0,0 }
        });
    writeCompactCsv("btc_mark.csv", {
        {      0, 130,130,130,130,  30000 },
        {  60000, 140,140,140,140,  90000 },
        { 120000, 150,150,150,150, 150000 }
        });
    const std::string mark_path = (tmpDir / "btc_mark.csv").string();

    writeFundingCsv("btc_funding.csv", {
        {  60000,  0.001, std::nullopt },
        { 120000, -0.002, 100.0 }
        });

    BinanceExchange ex(
        { BinanceExchange::SymbolDataset{
            "BTCUSDT",
            (tmpDir / "btc.csv").string(),
            std::optional<std::string>((tmpDir / "btc_funding.csv").string()),
            mark_path} },
        logger,
        /*balance*/ 1000.0);

    using QTrading::Dto::Trading::OrderSide;
    ASSERT_TRUE(ex.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
    auto mCh = ex.get_market_channel();

    BinanceExchange::StatusSnapshot snap{};

    ASSERT_TRUE(ex.step());
    mCh->Receive();
    ex.FillStatusSnapshot(snap);
    const double base = snap.wallet_balance;

    ASSERT_TRUE(ex.step());
    mCh->Receive();
    ex.FillStatusSnapshot(snap);
    const double after1 = snap.wallet_balance;
    EXPECT_NEAR(after1 - base, -0.14, 1e-6);

    ASSERT_TRUE(ex.step());
    mCh->Receive();
    ex.FillStatusSnapshot(snap);
    const double after2 = snap.wallet_balance;
    EXPECT_NEAR(after2 - base, 0.06, 1e-6);

    EXPECT_FALSE(ex.step());
}

TEST_F(BinanceExchangeFixture, FundingTimestampUsesInterpolatedMarkPriceBetweenBars)
{
    writeCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        { 120000, 120,120,120,120,1000,150000,100,1,0,0 }
        });
    writeCompactCsv("btc_mark.csv", {
        {      0, 100,100,100,100,  30000 },
        { 120000, 120,120,120,120, 150000 }
        });
    const std::string mark_path = (tmpDir / "btc_mark.csv").string();

    // Funding event falls between two kline timestamps and has no mark price.
    // Expected interpolated mark: 110.
    writeFundingCsv("btc_funding.csv", {
        {  60000,  0.001, std::nullopt }
        });

    BinanceExchange ex(
        { BinanceExchange::SymbolDataset{
            "BTCUSDT",
            (tmpDir / "btc.csv").string(),
            std::optional<std::string>((tmpDir / "btc_funding.csv").string()),
            mark_path} },
        logger,
        /*balance*/ 1000.0);

    using QTrading::Dto::Trading::OrderSide;
    ASSERT_TRUE(ex.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
    auto mCh = ex.get_market_channel();

    BinanceExchange::StatusSnapshot snap{};

    ASSERT_TRUE(ex.step());
    mCh->Receive();
    ex.FillStatusSnapshot(snap);
    const double after_entry = snap.wallet_balance;

    ASSERT_TRUE(ex.step());
    auto dto2 = mCh->Receive();
    ASSERT_TRUE(dto2.has_value());
    EXPECT_EQ(dto2->get()->Timestamp, 60000u);
    ex.FillStatusSnapshot(snap);
    const double after_funding = snap.wallet_balance;
    EXPECT_NEAR(after_funding - after_entry, -0.11, 1e-6);

    ASSERT_TRUE(ex.step());
    mCh->Receive();
    EXPECT_FALSE(ex.step());
}

TEST_F(BinanceExchangeFixture, PerpUnrealizedPnlUsesMarkDatasetWhenAvailable)
{
    writeCsv("btc.csv", {
        {      0, 100,100,100,100,1000,  30000,100,1,0,0 },
        {  60000, 100,100,100,100,1000,  90000,100,1,0,0 }
        });
    writeCompactCsv("btc_mark.csv", {
        {      0, 100,100,100,100,  30000 },
        {  60000,  80, 80, 80, 80,  90000 }
        });

    const std::string mark_path = (tmpDir / "btc_mark.csv").string();
    BinanceExchange ex(
        { BinanceExchange::SymbolDataset{
            "BTCUSDT",
            (tmpDir / "btc.csv").string(),
            std::nullopt,
            mark_path} },
        logger,
        /*balance*/ 1000.0);

    using QTrading::Dto::Trading::OrderSide;
    ASSERT_TRUE(ex.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
    auto mCh = ex.get_market_channel();

    BinanceExchange::StatusSnapshot snap{};
    ASSERT_TRUE(ex.step());
    mCh->Receive();

    ASSERT_TRUE(ex.step());
    mCh->Receive();
    ex.FillStatusSnapshot(snap);
    EXPECT_NEAR(snap.unrealized_pnl, -20.0, 1e-8);
}

TEST_F(BinanceExchangeFixture, StatusSnapshotPriceIncludesTradeMarkAndIndex)
{
    writeCsv("btc.csv", {
        {      0, 100,100,100,100,1000,  30000,100,1,0,0 }
        });
    writeCompactCsv("btc_mark.csv", {
        {      0, 123,123,123,123,  30000 }
        });

    const std::string mark_path = (tmpDir / "btc_mark.csv").string();
    BinanceExchange ex(
        { BinanceExchange::SymbolDataset{
            "BTCUSDT",
            (tmpDir / "btc.csv").string(),
            std::nullopt,
            mark_path,
            mark_path} },
        logger,
        /*balance*/ 1000.0);
    ASSERT_TRUE(ex.step());

    BinanceExchange::StatusSnapshot snap{};
    ex.FillStatusSnapshot(snap);
    ASSERT_EQ(snap.prices.size(), 1u);
    const auto& p = snap.prices[0];

    EXPECT_EQ(p.symbol, "BTCUSDT");
    EXPECT_TRUE(p.has_price);
    EXPECT_NEAR(p.price, 100.0, 1e-12);
    EXPECT_TRUE(p.has_trade_price);
    EXPECT_NEAR(p.trade_price, 100.0, 1e-12);
    EXPECT_TRUE(p.has_mark_price);
    EXPECT_NEAR(p.mark_price, 123.0, 1e-12);
    EXPECT_EQ(p.mark_price_source, static_cast<int32_t>(BinanceExchange::ReferencePriceSource::Raw));
    EXPECT_TRUE(p.has_index_price);
    EXPECT_NEAR(p.index_price, 123.0, 1e-12);
    EXPECT_EQ(p.index_price_source, static_cast<int32_t>(BinanceExchange::ReferencePriceSource::Raw));
}

TEST_F(BinanceExchangeFixture, MarkIndexDivergenceAddsUncertaintyBand)
{
    writeCsv("btc.csv", {
        {      0, 100,100,100,100,1000,  30000,100,1,0,0 }
        });
    writeCompactCsv("btc_mark.csv", {
        {      0, 120,120,120,120,  30000 }
        });
    writeCompactCsv("btc_index.csv", {
        {      0, 100,100,100,100,  30000 }
        });

    const std::string mark_path = (tmpDir / "btc_mark.csv").string();
    const std::string index_path = (tmpDir / "btc_index.csv").string();

    BinanceExchange ex(
        { BinanceExchange::SymbolDataset{
            "BTCUSDT",
            (tmpDir / "btc.csv").string(),
            std::nullopt,
            mark_path,
            index_path} },
        logger,
        /*balance*/ 1000.0);
    ex.set_uncertainty_band_bps(20.0);
    ASSERT_TRUE(ex.step());

    BinanceExchange::StatusSnapshot snap{};
    ex.FillStatusSnapshot(snap);

    ASSERT_EQ(snap.prices.size(), 1u);
    EXPECT_TRUE(snap.prices[0].has_mark_price);
    EXPECT_TRUE(snap.prices[0].has_index_price);
    EXPECT_NEAR(snap.prices[0].mark_price, 120.0, 1e-12);
    EXPECT_NEAR(snap.prices[0].index_price, 100.0, 1e-12);

    // base uncertainty(20 bps) + |mark-index|/index (2000 bps)
    EXPECT_NEAR(snap.uncertainty_band_bps, 2020.0, 1e-9);
    const double band = snap.uncertainty_band_bps / 10000.0;
    EXPECT_NEAR(snap.total_ledger_value_conservative, snap.total_ledger_value_base * (1.0 - band), 1e-9);
    EXPECT_NEAR(snap.total_ledger_value_optimistic, snap.total_ledger_value_base * (1.0 + band), 1e-9);
}

TEST_F(BinanceExchangeFixture, MarkIndexStressBlocksNewPerpOpeningOrders)
{
    writeCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 }
        });
    writeCompactCsv("btc_mark.csv", {
        {      0, 120,120,120,120, 30000 }
        });
    writeCompactCsv("btc_index.csv", {
        {      0, 100,100,100,100, 30000 }
        });

    BinanceExchange ex(
        { BinanceExchange::SymbolDataset{
            "BTCUSDT",
            (tmpDir / "btc.csv").string(),
            std::nullopt,
            (tmpDir / "btc_mark.csv").string(),
            (tmpDir / "btc_index.csv").string()} },
        logger,
        /*balance*/ 1000.0);
    ASSERT_TRUE(ex.step());

    using QTrading::Dto::Trading::OrderSide;
    EXPECT_FALSE(ex.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));

    BinanceExchange::StatusSnapshot snap{};
    ex.FillStatusSnapshot(snap);
    EXPECT_EQ(snap.basis_stress_symbols, 1u);
    EXPECT_EQ(snap.basis_stress_blocked_orders, 1u);
}

TEST_F(BinanceExchangeFixture, DisabledSimulatorRiskOverlayKeepsDiagnosticsButDoesNotBlockOrders)
{
    writeCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 }
        });
    writeCompactCsv("btc_mark.csv", {
        {      0, 120,120,120,120, 30000 }
        });
    writeCompactCsv("btc_index.csv", {
        {      0, 100,100,100,100, 30000 }
        });

    BinanceExchange ex(
        { BinanceExchange::SymbolDataset{
            "BTCUSDT",
            (tmpDir / "btc.csv").string(),
            std::nullopt,
            (tmpDir / "btc_mark.csv").string(),
            (tmpDir / "btc_index.csv").string()} },
        logger,
        /*balance*/ 1000.0);
    ex.set_simulator_risk_overlay_enabled(false);
    ASSERT_TRUE(ex.step());

    using QTrading::Dto::Trading::OrderSide;
    EXPECT_TRUE(ex.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));

    BinanceExchange::StatusSnapshot snap{};
    ex.FillStatusSnapshot(snap);
    EXPECT_EQ(snap.basis_warning_symbols, 0u);
    EXPECT_EQ(snap.basis_stress_symbols, 0u);
    EXPECT_EQ(snap.basis_stress_blocked_orders, 0u);
    EXPECT_GT(snap.uncertainty_band_bps, 0.0);
}

TEST_F(BinanceExchangeFixture, MarkIndexStressAllowsReduceOnlyPerpClose)
{
    writeCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000, 100,100,100,100,1000, 90000,100,1,0,0 },
        { 120000, 100,100,100,100,1000,150000,100,1,0,0 }
        });
    writeCompactCsv("btc_mark.csv", {
        {      0, 100,100,100,100, 30000 },
        {  60000, 120,120,120,120, 90000 },
        { 120000, 120,120,120,120,150000 }
        });
    writeCompactCsv("btc_index.csv", {
        {      0, 100,100,100,100, 30000 },
        {  60000, 100,100,100,100, 90000 },
        { 120000, 100,100,100,100,150000 }
        });

    BinanceExchange ex(
        { BinanceExchange::SymbolDataset{
            "BTCUSDT",
            (tmpDir / "btc.csv").string(),
            std::nullopt,
            (tmpDir / "btc_mark.csv").string(),
            (tmpDir / "btc_index.csv").string()} },
        logger,
        /*balance*/ 1000.0);

    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    ASSERT_TRUE(ex.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
    ASSERT_TRUE(ex.step());
    ASSERT_EQ(ex.get_all_positions().size(), 1u);

    // Move into stress basis regime.
    ASSERT_TRUE(ex.step());

    EXPECT_TRUE(ex.perp.place_order(
        "BTCUSDT",
        1.0,
        OrderSide::Sell,
        PositionSide::Both,
        true));
}

TEST_F(BinanceExchangeFixture, MarkIndexWarningAutoDeleveragesPerpLeverage)
{
    writeCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 }
        });
    writeCompactCsv("btc_mark.csv", {
        {      0, 100.6,100.6,100.6,100.6, 30000 }
        });
    writeCompactCsv("btc_index.csv", {
        {      0, 100,100,100,100, 30000 }
        });

    BinanceExchange ex(
        { BinanceExchange::SymbolDataset{
            "BTCUSDT",
            (tmpDir / "btc.csv").string(),
            std::nullopt,
            (tmpDir / "btc_mark.csv").string(),
            (tmpDir / "btc_index.csv").string()} },
        logger,
        /*balance*/ 1000.0);

    ex.set_symbol_leverage("BTCUSDT", 20.0);
    ex.set_basis_risk_leverage_caps(/*warning*/7.0, /*stress*/3.0);
    ASSERT_TRUE(ex.step());

    EXPECT_DOUBLE_EQ(ex.get_symbol_leverage("BTCUSDT"), 7.0);

    BinanceExchange::StatusSnapshot snap{};
    ex.FillStatusSnapshot(snap);
    EXPECT_EQ(snap.basis_warning_symbols, 1u);
    EXPECT_EQ(snap.basis_stress_symbols, 0u);
}

TEST_F(BinanceExchangeFixture, FundingInterpolationPrefersConfiguredMarkDataset)
{
    writeCsv("btc.csv", {
        {      0, 100,100,100,100,1000,  30000,100,1,0,0 },
        { 120000, 120,120,120,120,1000, 150000,100,1,0,0 }
        });
    writeCompactCsv("btc_mark.csv", {
        {      0, 200,200,200,200,  30000 },
        { 120000, 220,220,220,220, 150000 }
        });
    writeFundingCsv("btc_funding.csv", {
        {  60000,  0.001, std::nullopt }
        });

    const std::string mark_path = (tmpDir / "btc_mark.csv").string();
    BinanceExchange ex(
        { BinanceExchange::SymbolDataset{
            "BTCUSDT",
            (tmpDir / "btc.csv").string(),
            std::optional<std::string>((tmpDir / "btc_funding.csv").string()),
            mark_path} },
        logger,
        /*balance*/ 1000.0);

    using QTrading::Dto::Trading::OrderSide;
    ASSERT_TRUE(ex.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
    auto mCh = ex.get_market_channel();

    BinanceExchange::StatusSnapshot snap{};
    ASSERT_TRUE(ex.step());
    mCh->Receive();
    ex.FillStatusSnapshot(snap);
    const double after_entry = snap.wallet_balance;

    ASSERT_TRUE(ex.step());
    auto dto2 = mCh->Receive();
    ASSERT_TRUE(dto2.has_value());
    EXPECT_EQ(dto2->get()->Timestamp, 60000u);
    ex.FillStatusSnapshot(snap);
    const double after_funding = snap.wallet_balance;

    // Mark interpolation from configured mark dataset:
    // t=0 => 200, t=120000 => 220, so t=60000 => 210.
    // 1 BTC long with rate 0.001 pays 0.21.
    EXPECT_NEAR(after_funding - after_entry, -0.21, 1e-6);
}

TEST_F(BinanceExchangeFixture, FundingApplyTimingControlsSameTimestampFunding)
{
    writeCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000, 200,200,200,200,1000, 90000,100,1,0,0 }
        });

    writeFundingCsv("btc_funding.csv", {
        {  60000, 0.001, 100.0 }
        });

    auto run_case = [&](BinanceExchange::FundingApplyTiming timing) -> double {
        BinanceExchange ex(
            { {"BTCUSDT", (tmpDir / "btc.csv").string(),
                std::optional<std::string>((tmpDir / "btc_funding.csv").string())} },
            logger,
            /*balance*/ 1000.0);
        ex.set_funding_apply_timing(timing);

        auto mCh = ex.get_market_channel();
        using QTrading::Dto::Trading::OrderSide;
        BinanceExchange::StatusSnapshot snap{};

        if (!ex.step()) {
            ADD_FAILURE() << "step#1 failed";
            return std::numeric_limits<double>::quiet_NaN();
        }
        mCh->Receive();
        if (!ex.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy)) {
            ADD_FAILURE() << "place_order failed";
            return std::numeric_limits<double>::quiet_NaN();
        }

        if (!ex.step()) {
            ADD_FAILURE() << "step#2 failed";
            return std::numeric_limits<double>::quiet_NaN();
        }
        mCh->Receive();
        ex.FillStatusSnapshot(snap);
        return snap.wallet_balance;
    };

    const double before_matching = run_case(BinanceExchange::FundingApplyTiming::BeforeMatching);
    const double after_matching = run_case(BinanceExchange::FundingApplyTiming::AfterMatching);

    // Same-timestamp funding at mark=100, rate=0.001 on long => 0.1 USDT difference.
    EXPECT_NEAR(before_matching - after_matching, 0.1, 1e-6);
}

TEST_F(BinanceExchangeFixture, MarketSnapshotFundingUsesPreviousPeriodUntilUpdateIsApplied)
{
    writeCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000, 200,200,200,200,1000, 90000,100,1,0,0 }
        });

    writeFundingCsv("btc_funding.csv", {
        {      0, 0.0002, 100.0 },
        {  60000, 0.0010, 100.0 }
        });

    auto funding_seen_at_step2 = [&](BinanceExchange::FundingApplyTiming timing) -> double {
        BinanceExchange ex(
            { {"BTCUSDT", (tmpDir / "btc.csv").string(),
                std::optional<std::string>((tmpDir / "btc_funding.csv").string())} },
            logger,
            /*balance*/ 1000.0);
        ex.set_funding_apply_timing(timing);

        auto mCh = ex.get_market_channel();

        if (!ex.step()) {
            ADD_FAILURE() << "step#1 failed";
            return std::numeric_limits<double>::quiet_NaN();
        }
        auto dto1 = mCh->Receive();
        if (!dto1.has_value() || dto1->get()->Timestamp != 0u) {
            ADD_FAILURE() << "unexpected dto1";
            return std::numeric_limits<double>::quiet_NaN();
        }

        if (!ex.step()) {
            ADD_FAILURE() << "step#2 failed";
            return std::numeric_limits<double>::quiet_NaN();
        }
        auto dto2 = mCh->Receive();
        if (!dto2.has_value() || dto2->get()->Timestamp != 60000u) {
            ADD_FAILURE() << "unexpected dto2";
            return std::numeric_limits<double>::quiet_NaN();
        }
        if (!dto2->get()->symbols ||
            dto2->get()->symbols->size() != 1u ||
            (*dto2->get()->symbols)[0] != "BTCUSDT" ||
            dto2->get()->funding_by_id.size() != 1u ||
            !dto2->get()->funding_by_id[0].has_value())
        {
            ADD_FAILURE() << "unexpected funding snapshot shape";
            return std::numeric_limits<double>::quiet_NaN();
        }
        return dto2->get()->funding_by_id[0]->Rate;
    };

    const double before_matching = funding_seen_at_step2(BinanceExchange::FundingApplyTiming::BeforeMatching);
    const double after_matching = funding_seen_at_step2(BinanceExchange::FundingApplyTiming::AfterMatching);

    // BeforeMatching applies and publishes the new rate at t=60000.
    EXPECT_NEAR(before_matching, 0.0010, 1e-12);
    // AfterMatching still sees previous period at the same timestamp.
    EXPECT_NEAR(after_matching, 0.0002, 1e-12);
}

TEST_F(BinanceExchangeFixture, FundingWithoutMarkSourceIsSkipped)
{
    writeCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 }
        });

    // Funding only appears much later with no mark price.
    writeFundingCsv("btc_funding.csv", {
        { 600000, 0.001, std::nullopt }
        });

    BinanceExchange ex(
        { {"BTCUSDT", (tmpDir / "btc.csv").string(),
            std::optional<std::string>((tmpDir / "btc_funding.csv").string())} },
        logger,
        /*balance*/ 1000.0);

    auto mCh = ex.get_market_channel();
    using QTrading::Dto::Trading::OrderSide;
    BinanceExchange::StatusSnapshot snap{};

    ASSERT_TRUE(ex.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));

    ASSERT_TRUE(ex.step());
    mCh->Receive();
    ex.FillStatusSnapshot(snap);
    const double after_entry = snap.wallet_balance;

    // Next step is funding-only timestamp (no kline). Without mark source, funding is skipped.
    ASSERT_TRUE(ex.step());
    mCh->Receive();
    ex.FillStatusSnapshot(snap);
    const double after_funding_ts = snap.wallet_balance;

    EXPECT_NEAR(after_funding_ts, after_entry, 1e-8);
    EXPECT_GE(snap.funding_skipped_no_mark, 1u);
    EXPECT_EQ(snap.funding_applied_events, 0u);
}

TEST_F(BinanceExchangeFixture, NoFundingPathKeepsBalance)
{
    writeCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000, 110,110,110,110,1000, 90000,100,1,0,0 }
        });

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger, /*balance*/ 1000.0);

    using QTrading::Dto::Trading::OrderSide;
    ASSERT_TRUE(ex.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
    auto mCh = ex.get_market_channel();

    BinanceExchange::StatusSnapshot snap{};

    ASSERT_TRUE(ex.step());
    mCh->Receive();
    ex.FillStatusSnapshot(snap);
    const double base = snap.wallet_balance;

    ASSERT_TRUE(ex.step());
    mCh->Receive();
    ex.FillStatusSnapshot(snap);
    const double after = snap.wallet_balance;

    EXPECT_NEAR(after, base, 1e-8);
}

TEST_F(BinanceExchangeFixture, ExplicitInstrumentTypeSpotWorksWithoutSuffixSymbol)
{
    writeCsv("btc_cash.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000, 100,100,100,100,1000, 90000,100,1,0,0 }
        });

    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 0.0;

    BinanceExchange ex(
        { {"BTCUSDT",
            (tmpDir / "btc_cash.csv").string(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            QTrading::Dto::Trading::InstrumentType::Spot} },
        logger,
        cfg);

    using QTrading::Dto::Trading::OrderSide;
    // Spot without inventory should reject naked sell.
    EXPECT_FALSE(ex.spot.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Sell));

    ASSERT_TRUE(ex.spot.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy));
    ASSERT_TRUE(ex.step());
    ASSERT_EQ(ex.get_all_positions().size(), 1u);

    // Spot leverage stays unlevered even when caller tries to set it.
    ex.set_symbol_leverage("BTCUSDT", 20.0);
    EXPECT_DOUBLE_EQ(ex.get_symbol_leverage("BTCUSDT"), 1.0);
}

TEST_F(BinanceExchangeFixture, LegacyLeverageWrapperMatchesPerpFacade)
{
    writeCsv("btc_perp.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000, 100,100,100,100,1000, 90000,100,1,0,0 }
        });

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc_perp.csv").string()} }, logger, /*balance*/ 1000.0);

    ex.set_symbol_leverage("BTCUSDT", 15.0);
    EXPECT_DOUBLE_EQ(ex.get_symbol_leverage("BTCUSDT"), 15.0);
    EXPECT_DOUBLE_EQ(ex.perp.get_symbol_leverage("BTCUSDT"), 15.0);

    ex.perp.set_symbol_leverage("BTCUSDT", 8.0);
    EXPECT_DOUBLE_EQ(ex.get_symbol_leverage("BTCUSDT"), 8.0);
}

TEST_F(BinanceExchangeFixture, StatusSnapshotExposesDualLedgerTotals)
{
    writeCsv("btc_spot.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000, 110,110,110,110,1000, 90000,100,1,0,0 }
        });

    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 500.0;

    BinanceExchange ex(
        { {"BTCUSDT",
            (tmpDir / "btc_spot.csv").string(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            QTrading::Dto::Trading::InstrumentType::Spot} },
        logger,
        cfg);

    using QTrading::Dto::Trading::OrderSide;
    ASSERT_TRUE(ex.spot.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy));
    ASSERT_TRUE(ex.step());

    BinanceExchange::StatusSnapshot snap{};
    ex.FillStatusSnapshot(snap);

    EXPECT_GT(snap.spot_inventory_value, 0.0);
    EXPECT_NEAR(
        snap.spot_ledger_value,
        snap.spot_cash_balance + snap.spot_inventory_value,
        1e-9);
    EXPECT_NEAR(
        snap.total_ledger_value,
        snap.perp_margin_balance + snap.spot_ledger_value,
        1e-9);
}

TEST_F(BinanceExchangeFixture, StatusSnapshotOutputsUncertaintyBands)
{
    writeCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 }
        });

    Account::AccountInitConfig cfg;
    cfg.spot_initial_cash = 500.0;
    cfg.perp_initial_wallet = 500.0;

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger, cfg);
    ex.set_uncertainty_band_bps(200.0); // 2%
    ASSERT_TRUE(ex.step());

    BinanceExchange::StatusSnapshot snap{};
    ex.FillStatusSnapshot(snap);

    EXPECT_NEAR(snap.uncertainty_band_bps, 200.0, 1e-12);
    EXPECT_NEAR(snap.total_ledger_value_base, snap.total_ledger_value, 1e-12);
    EXPECT_NEAR(snap.total_ledger_value_conservative, snap.total_ledger_value * 0.98, 1e-9);
    EXPECT_NEAR(snap.total_ledger_value_optimistic, snap.total_ledger_value * 1.02, 1e-9);
}

TEST_F(BinanceExchangeFixture, StrictModeAllowsDatasetSymbolWithoutExplicitInstrumentType)
{
    writeCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 }
        });

    Account::AccountInitConfig cfg;
    cfg.perp_initial_wallet = 1000.0;
    cfg.strict_binance_mode = true;

    BinanceExchange ex(
        { BinanceExchange::SymbolDataset{ "BTCUSDT", (tmpDir / "btc.csv").string(), std::nullopt } },
        logger,
        cfg);

    using QTrading::Dto::Trading::OrderSide;
    EXPECT_TRUE(ex.perp.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy));
}

TEST_F(BinanceExchangeFixture, StrictModeRejectsOrdersForUnknownDatasetSymbol)
{
    writeCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000, 101,101,101,101,1000, 90000,100,1,0,0 }
        });

    Account::AccountInitConfig cfg;
    cfg.perp_initial_wallet = 1000.0;
    cfg.strict_binance_mode = true;

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger, cfg);
    using QTrading::Dto::Trading::OrderSide;

    EXPECT_FALSE(ex.perp.place_order("ETHUSDT", 1.0, 100.0, OrderSide::Buy));

    ex.set_order_latency_bars(1);
    EXPECT_TRUE(ex.perp.place_order("ETHUSDT", 1.0, 100.0, OrderSide::Buy));
    auto pending = ex.drain_async_order_acks();
    ASSERT_EQ(pending.size(), 1u);
    EXPECT_EQ(pending[0].status, BinanceExchange::AsyncOrderAck::Status::Pending);

    auto mCh = ex.get_market_channel();
    ASSERT_TRUE(ex.step());
    mCh->Receive();

    auto resolved = ex.drain_async_order_acks();
    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_EQ(resolved[0].status, BinanceExchange::AsyncOrderAck::Status::Rejected);
    EXPECT_EQ(resolved[0].reject_code, Account::OrderRejectInfo::Code::UnknownSymbol);
    EXPECT_EQ(resolved[0].binance_error_code, -1121);
}


