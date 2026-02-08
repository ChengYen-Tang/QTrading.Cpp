#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <optional>
#include <type_traits>
#include <limits>

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
    EXPECT_TRUE(dto1->get()->klines_by_id[btc_id].has_value());
    EXPECT_FALSE(dto1->get()->klines_by_id[eth_id].has_value());

    // ---------- step #2  (t = 30 000) -----
    ASSERT_TRUE(ex.step());
    auto dto2 = mCh->Receive();
    EXPECT_EQ(dto2->get()->Timestamp, 30000u);
    EXPECT_FALSE(dto2->get()->klines_by_id[btc_id].has_value());
    EXPECT_TRUE(dto2->get()->klines_by_id[eth_id].has_value());

    // ---------- step #3  (t = 60 000) -----
    ASSERT_TRUE(ex.step());
    auto dto3 = mCh->Receive();
    EXPECT_EQ(dto3->get()->Timestamp, 60000u);
    EXPECT_TRUE(dto3->get()->klines_by_id[btc_id].has_value());
    EXPECT_TRUE(dto3->get()->klines_by_id[eth_id].has_value());

    // ---------- step #4  (EOF) ------------
    EXPECT_FALSE(ex.step());          // nothing left
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

TEST_F(BinanceExchangeFixture, FundingAppliedAndDeduped)
{
    writeCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        {  60000, 110,110,110,110,1000, 90000,100,1,0,0 },
        { 120000, 120,120,120,120,1000,150000,100,1,0,0 }
        });

    writeFundingCsv("btc_funding.csv", {
        {  60000,  0.001, std::nullopt },
        { 120000, -0.002, 100.0 }
        });

    BinanceExchange ex(
        { {"BTCUSDT", (tmpDir / "btc.csv").string(),
            std::optional<std::string>((tmpDir / "btc_funding.csv").string())} },
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
    EXPECT_NEAR(after1 - base, -0.11, 1e-6);

    ASSERT_TRUE(ex.step());
    mCh->Receive();
    ex.FillStatusSnapshot(snap);
    const double after2 = snap.wallet_balance;
    EXPECT_NEAR(after2 - base, 0.09, 1e-6);

    EXPECT_FALSE(ex.step());
}

TEST_F(BinanceExchangeFixture, FundingTimestampUsesInterpolatedMarkPriceBetweenBars)
{
    writeCsv("btc.csv", {
        {      0, 100,100,100,100,1000, 30000,100,1,0,0 },
        { 120000, 120,120,120,120,1000,150000,100,1,0,0 }
        });

    // Funding event falls between two kline timestamps and has no mark price.
    // Expected interpolated mark: 110.
    writeFundingCsv("btc_funding.csv", {
        {  60000,  0.001, std::nullopt }
        });

    BinanceExchange ex(
        { {"BTCUSDT", (tmpDir / "btc.csv").string(),
            std::optional<std::string>((tmpDir / "btc_funding.csv").string())} },
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

TEST_F(BinanceExchangeFixture, FundingMarkPriceMaxAgeSkipsStaleOneSidedInterpolation)
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
    ex.set_funding_mark_price_max_age_ms(60000); // 1 minute max-age

    auto mCh = ex.get_market_channel();
    using QTrading::Dto::Trading::OrderSide;
    BinanceExchange::StatusSnapshot snap{};

    ASSERT_TRUE(ex.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));

    ASSERT_TRUE(ex.step());
    mCh->Receive();
    ex.FillStatusSnapshot(snap);
    const double after_entry = snap.wallet_balance;

    // Next step is funding-only timestamp (no kline). Funding should be skipped due to stale fallback.
    ASSERT_TRUE(ex.step());
    mCh->Receive();
    ex.FillStatusSnapshot(snap);
    const double after_funding_ts = snap.wallet_balance;

    EXPECT_NEAR(after_funding_ts, after_entry, 1e-8);
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


