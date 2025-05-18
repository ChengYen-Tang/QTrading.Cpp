//  BinanceExchangeTest.cpp
//  ---------------------------------------------------------------
//  Test-suite for BinanceExchange (simulator)
//  Uses Google-Test test-fixtures (TEST_F) so that every test has
//  its own SetUp / TearDown for resource management.
//  ---------------------------------------------------------------

#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

using namespace QTrading::Infra::Exchanges::BinanceSim;
using namespace QTrading::Dto::Market::Binance;
using namespace QTrading::Utils::Queue;
namespace fs = std::filesystem;

class MockLogger : public QTrading::Log::Logger {
    public:
        MockLogger(const std::string &dir) : QTrading::Log::Logger(dir) {}

    protected:
        void Consume() override
        {
		}
};

/* ======================================================================= */
/*  Base Fixture – provides tmpDir + helper to generate tiny CSV files     */
/* ======================================================================= */
class BinanceExchangeFixture : public ::testing::Test {
protected:
    fs::path tmpDir;          // …/<gtest name>/ directory
    std::shared_ptr<QTrading::Log::Logger> logger;

    /** helper – write minimal Binance 1-minute CSV  */
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

    /* ---------- lifecycle ---------- */
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

    void TearDown() override
    {
		logger->Stop();            // stop logger thread
        fs::remove_all(tmpDir);     // clean everything we created
    }
};

/* ======================================================================= */
/*  Test-Case 1 : Multi-symbol sync + std::nullopt                         */
/* ======================================================================= */
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
    EXPECT_TRUE(dto1->get()->klines.at("BTCUSDT").has_value());
    EXPECT_FALSE(dto1->get()->klines.at("ETHUSDT").has_value());

    // ---------- step #2  (t = 30 000) -----
    ASSERT_TRUE(ex.step());
    auto dto2 = mCh->Receive();
    EXPECT_EQ(dto2->get()->Timestamp, 30000u);
    EXPECT_FALSE(dto2->get()->klines.at("BTCUSDT").has_value());
    EXPECT_TRUE(dto2->get()->klines.at("ETHUSDT").has_value());

    // ---------- step #3  (t = 60 000) -----
    ASSERT_TRUE(ex.step());
    auto dto3 = mCh->Receive();
    EXPECT_EQ(dto3->get()->Timestamp, 60000u);
    EXPECT_TRUE(dto3->get()->klines.at("BTCUSDT").has_value());
    EXPECT_TRUE(dto3->get()->klines.at("ETHUSDT").has_value());

    // ---------- step #4  (EOF) ------------
    EXPECT_FALSE(ex.step());          // nothing left
}

/* ======================================================================= */
/*  Test-Case 2 : Debounced position / order channels                      */
/* ======================================================================= */
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

    /* ---- create an order -> should appear next step -- */
    ex.place_order("BTCUSDT", 1.0, 1.0, true);

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

/* ======================================================================= */
/*  Test-Case 3 : Snapshot getters                                         */
/* ======================================================================= */
TEST_F(BinanceExchangeFixture, SnapshotConsistent)
{
    writeCsv("btc.csv", {
        {0,1,1,1,1,10, 30000,10,1,0,0}
        });

    BinanceExchange ex({ {"BTCUSDT",(tmpDir / "btc.csv").string()} }, logger, /*balance*/ 500.0);

    // 1) No positions yet
    EXPECT_TRUE(ex.get_all_positions().empty());

    // 2) Place market order 0.5 BTC long
    ex.place_order("BTCUSDT", 0.5, 0.0, true);
    ex.step();                    // fills immediately

    EXPECT_EQ(ex.get_all_positions().size(), 1u);
    EXPECT_TRUE(ex.get_all_open_orders().empty());
}
