#define _USE_MATH_DEFINES
#include <cmath>
#include <gtest/gtest.h>
#include <filesystem>
#include <thread>

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Aggregator/BinanceHourAggregator.hpp"
#include <boost/filesystem/fstream.hpp>

using namespace QTrading;
using namespace QTrading::Infra::Exchanges::BinanceSim;
using namespace QTrading::DataPreprocess::Aggregator;
using namespace QTrading::Dto::Market::Binance;
using AggPtr = std::shared_ptr<QTrading::DataPreprocess::Dto::AggregateKline>;
namespace fs = std::filesystem;

class AggregatorFixture : public ::testing::Test {
protected:
    fs::path tmpDir;
    std::shared_ptr<BinanceExchange>              ex;
    std::unique_ptr<BinanceHourAggregator>        agg;

    /* ---------- helpers ------------------------------------------------ */
    void writeCsv(const std::string& path,
        std::size_t nMin,
        uint64_t   startOffsetMin = 0,
        double     priceBase = 1.0,
        double     vol = 100,
        uint64_t   driftMs = 0,
        const std::function<double(size_t)>& priceFn = {})
    {
        std::filesystem::path fullPath = tmpDir / path;
        bool fileExists = std::filesystem::exists(fullPath);

        std::ofstream f;
        if (fileExists) {
            f.open(fullPath, std::ios::app);
        }
        else {
            f.open(fullPath);
            f << "openTime,open,high,low,close,volume,closeTime,quoteVol,tradeCnt,takerBB,takerBQ\n";
        }

        for (std::size_t i = 0; i < nMin; ++i) {
            std::size_t idx = i + startOffsetMin;
            uint64_t openTs = idx * 60'000ULL + 1'704'038'400'000ULL;
            uint64_t closeTs = openTs + 60'000ULL - 1000ULL + driftMs;
            double   price = priceFn ? priceFn(idx) : priceBase + idx;

            f << openTs << ',' << price << ',' << price << ',' << price << ','
                << price << ',' << vol << ',' << closeTs << ','
                << vol << ",1,0,0\n";
        }
    }

    void SetUp() override
    {
        // Each test gets its own directory, eg. "tmp/BinanceExchangeFixture_StepOrdering"
        tmpDir = fs::temp_directory_path() /
            (std::string("QTradingTest_") + ::testing::UnitTest::GetInstance()
                ->current_test_info()->name());
        fs::create_directories(tmpDir);
    }

    void TearDown() override
    {
        if (ex)  ex->close();   // ① unblock aggregator
        if (agg) agg->stop();   // ② join worker thread
        fs::remove_all(tmpDir);
    }
};

/* ================================================================== */
/* 1. Continuous 120 minutes – volume / O/H/L/C correctness           */
/* ================================================================== */
TEST_F(AggregatorFixture, BasicAggregate)
{
    writeCsv("btc60.csv", 60);

    ex = std::make_shared<BinanceExchange>(
        std::vector<std::pair<std::string, std::string>>{
            {"BTCUSDT", (tmpDir / "btc60.csv").string()}});

    agg = std::make_unique<BinanceHourAggregator>(ex, 1);
    agg->start();
    auto ch = agg->get_market_channel();

    for (int i = 0; i < 60; ++i) ex->step();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));

    AggPtr last;
    while (auto p = ch->TryReceive()) last = p.value();
    ASSERT_TRUE(last);

    const auto& bars = last->HistoricalKlines.at("BTCUSDT");
    ASSERT_EQ(bars.size(), 1u);

    const auto& bar = bars.front();
	EXPECT_EQ(bar.Timestamp, 1'704'038'400'000ULL);
	EXPECT_EQ(bar.CloseTime, 1'704'038'400'000ULL + (59 * 60'000ULL) + 59'000ULL);
    EXPECT_EQ(bar.OpenPrice, 1.0);         // first minute
    EXPECT_EQ(bar.ClosePrice, 1.0 + 59);    // last minute
    EXPECT_EQ(bar.HighPrice, 1.0 + 59);
    EXPECT_EQ(bar.LowPrice, 1.0);
    EXPECT_EQ(bar.Volume, 60 * 100);
}

/* ================================================================== */
/* 2. Missing minutes (nullopt) handled                               */
/* ================================================================== */
TEST_F(AggregatorFixture, DriftTolerance)
{
    writeCsv("btcDrift.csv", 120, 7);

    ex = std::make_shared<BinanceExchange>(
        std::vector<std::pair<std::string, std::string>>{
            {"BTCUSDT", (tmpDir / "btcDrift.csv").string()}});
    agg = std::make_unique<BinanceHourAggregator>(ex, 2);
    agg->start();
    auto ch = agg->get_market_channel();

    for (int i = 0; i < 120; ++i) ex->step();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));

    AggPtr last;
    while (auto p = ch->TryReceive()) last = p.value();
    ASSERT_TRUE(last);

    const auto& dq = last->HistoricalKlines.at("BTCUSDT");
    ASSERT_EQ(dq.size(), 2u);                // both hours detected correctly
    EXPECT_EQ(dq.front().CloseTime, 1704046019000ULL);
	EXPECT_EQ(dq.front().Timestamp, 1704045600000ULL);
    EXPECT_EQ(dq.back().CloseTime, 1704045599000ULL);
    EXPECT_EQ(dq.back().Timestamp, 1704042000000ULL);
}

/* ================================================================== */
/* 3. Sliding window size enforcement                                 */
/* ================================================================== */
TEST_F(AggregatorFixture, WindowClipped)
{
    writeCsv("btc240.csv", 240);       // 4 hours

    ex = std::make_shared<BinanceExchange>(
        std::vector<std::pair<std::string, std::string>>{
            {"BTCUSDT", (tmpDir / "btc240.csv").string()}});
    agg = std::make_unique<BinanceHourAggregator>(ex, 3);        // keep 3 hours
    agg->start();
    auto ch = agg->get_market_channel();

    for (int i = 0; i < 240; ++i) ex->step();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));

    AggPtr last;
    while (auto p = ch->TryReceive()) last = p.value();
    ASSERT_TRUE(last);

    const auto& dq = last->HistoricalKlines.at("BTCUSDT");
    EXPECT_EQ(dq.size(), 3u);                 // window trimmed to 3
}

/* ================================================================= */
/* 4.  Multi‑symbol with different start offsets                      */
/* ================================================================= */
TEST_F(AggregatorFixture, MultiSymbolOverlap)
{
    /* BTC: minute 0‑59 */
    writeCsv("btc.csv", 60, 0, 100, 10);
    /* ETH: minute 30‑89 (starts 30 min late) */
    writeCsv("eth.csv", 60, 30, 200, 20);
    /* XRP: minute 90‑149 (starts 90 min late = 2nd hour) */
    writeCsv("xrp.csv", 60, 90, 300, 30);

    ex = std::make_shared<BinanceExchange>(std::vector<std::pair<std::string, std::string>>{
        {"BTCUSDT",(tmpDir / "btc.csv").string()},
        {"ETHUSDT",(tmpDir / "eth.csv").string()},
        {"XRPUSDT",(tmpDir / "xrp.csv").string()} });
    agg = std::make_unique<BinanceHourAggregator>(ex, 2);
    agg->start();
    auto ch = agg->get_market_channel();

    for (int i = 0; i < 150; ++i) ex->step();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    AggPtr last;
    while (auto p = ch->TryReceive()) last = p.value();
    ASSERT_TRUE(last);

    /* BTC should have exactly 2 hourly bars */
    EXPECT_EQ(last->HistoricalKlines.at("BTCUSDT").size(), 1u);
    /* ETH should have 2 bars (first: 30 mins, second: 30 mins) */
    EXPECT_EQ(last->HistoricalKlines.at("ETHUSDT").size(), 2u);
    /* XRP only started in 2nd hour → 1 bar */
    EXPECT_EQ(last->HistoricalKlines.at("XRPUSDT").size(), 2u);
}

/* ================================================================= */
/* 7.  Large time jump (> 1h) triggers immediate rollover            */
/* ================================================================= */
TEST_F(AggregatorFixture, LargeJumpRollover)
{
    /* First minute at t=0, second minute at t=90min → jump 1.5h */
    writeCsv("jump.csv", 1, 0, 1.0, 10);
    writeCsv("jump.csv", 1, 180, 101.0, 20, 0, [](size_t i) {return 101.0; }); // append

    ex = std::make_shared<BinanceExchange>(std::vector<std::pair<std::string, std::string>>{ {"JMP",(tmpDir / "jump.csv").string()} });
    agg = std::make_unique<BinanceHourAggregator>(ex, 2);
    agg->start();
    auto ch = agg->get_market_channel();

    /* only 2 steps needed because dataset has 2 rows */
    ex->step(); ex->step();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    AggPtr last;
    while (auto p = ch->TryReceive()) last = p.value();
    ASSERT_TRUE(last);

    /* first bar must already be finalised (volume 10) */
    ASSERT_EQ(last->HistoricalKlines.at("JMP").size(), 1u);
    const auto& bar0 = last->HistoricalKlines.at("JMP").front();
    EXPECT_EQ(bar0.Timestamp, 1704049200000ULL);
    EXPECT_EQ(bar0.CloseTime, 1704049259000ULL);
}

/* ================================================================= */
/* 8.  Mid‑hour gap (symbol misses 10 minutes)                       */
/* ================================================================= */
TEST_F(AggregatorFixture, GapInsideHour)
{
    /* Generate minutes 0‑24 and 35‑59 (10‑minute gap) */
    writeCsv("gap.csv", 25, 0, 50, 5);          // 0‑24
    writeCsv("gap.csv", 25, 35, 75, 5, 0,        // 35‑59
        [](size_t i) { return 75 + i; });

    ex = std::make_shared<BinanceExchange>(std::vector<std::pair<std::string, std::string>>{ {"GAP",(tmpDir / "gap.csv").string()} });
    agg = std::make_unique<BinanceHourAggregator>(ex, 1);
    agg->start();
    auto ch = agg->get_market_channel();

    for (int i = 0; i < 50; ++i) ex->step();          // 50 rows total
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    AggPtr last;
    while (auto p = ch->TryReceive()) last = p.value();
    ASSERT_TRUE(last);

    const auto& bar = last->HistoricalKlines.at("GAP").front();
    /* Volume = 50 rows * 5 vol = 250 */
    EXPECT_EQ(bar.Volume, 250.0);
    /* High must be from later part ( >= 75+24 ) */
    EXPECT_GE(bar.HighPrice, 75 + 24);
    /* Low must be from early part ( 50 ) */
    EXPECT_EQ(bar.LowPrice, 50.0);
}

/* ================================================================= */
/* 9.  Sliding window enforced per‑symbol                            */
/* ================================================================= */
TEST_F(AggregatorFixture, SlidingWindowMultiSymbol)
{
    /* 4 hours for each symbol */
    writeCsv("btc4h.csv", 240, 0, 1.0, 10);
    writeCsv("eth4h.csv", 240, 0, 2.0, 20);

    ex = std::make_shared<BinanceExchange>(std::vector<std::pair<std::string, std::string>>{
        {"BTCUSDT",(tmpDir / "btc4h.csv").string()},
        {"ETHUSDT",(tmpDir / "eth4h.csv").string()} });
    agg = std::make_unique<BinanceHourAggregator>(ex, 2);
    agg->start();
    auto ch = agg->get_market_channel();

    for (int i = 0; i < 240; ++i) ex->step();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    AggPtr last;
    while (auto p = ch->TryReceive()) last = p.value();
    ASSERT_TRUE(last);

    EXPECT_EQ(last->HistoricalKlines.at("BTCUSDT").size(), 2u);
    EXPECT_EQ(last->HistoricalKlines.at("ETHUSDT").size(), 2u);
}
