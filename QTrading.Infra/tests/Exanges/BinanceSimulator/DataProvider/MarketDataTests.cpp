#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <Exanges/BinanceSimulator/DataProvider/MarketData.hpp>

static const char* test_csv_filename = "test_kline_data.csv";

class MarketDataTests : public ::testing::Test {
protected:
    void SetUp() override {
        boost::filesystem::ofstream ofs(test_csv_filename);
        ofs << "OpenTime,OpenPrice,HighPrice,LowPrice,ClosePrice,Volume,CloseTime,QuoteVolume,TradeCount,TakerBuyBaseVolume,TakerBuyQuoteVolume\n";
        ofs << "2020-01-01 00:00:00,7000,7050,6950,7020,100,2020-01-01 00:04:59,700000,50,20,140000\n";
        ofs << "2020-01-01 00:05:00,7020,7100,7000,7050,200,2020-01-01 00:09:59,1400000,80,40,280000\n";
        ofs.close();
    }

    void TearDown() override {
        // 可以使用 boost::filesystem::remove 取代 std::remove
        boost::filesystem::remove(test_csv_filename);
    }
};

TEST_F(MarketDataTests, LoadCsvAndCheckCount) {
    MarketData md("BTCUSDT", test_csv_filename);
    EXPECT_EQ(md.get_klines_count(), 2u);
}

TEST_F(MarketDataTests, GetLatestKline) {
    MarketData md("BTCUSDT", test_csv_filename);
    auto latest = md.get_latest_kline();
    // 應該是第二筆 (index = 1)
    EXPECT_EQ(latest.OpenTime, "2020-01-01 00:05:00");
    EXPECT_DOUBLE_EQ(latest.OpenPrice, 7020.0);
    EXPECT_DOUBLE_EQ(latest.ClosePrice, 7050.0);
    EXPECT_DOUBLE_EQ(latest.Volume, 200.0);
    EXPECT_EQ(latest.TradeCount, 80);
}

TEST_F(MarketDataTests, GetKlineOutOfRange) {
    MarketData md("BTCUSDT", test_csv_filename);
    // 總共只有 2 筆, index = 2 應該拋出 out_of_range
    EXPECT_THROW(md.get_kline(2), std::out_of_range);
}

TEST_F(MarketDataTests, GetSymbolAndFirstKline) {
    MarketData md("BTCUSDT", test_csv_filename);
    // 檢查第一筆
    const auto& kline0 = md.get_kline(0);
    EXPECT_EQ(kline0.OpenTime, "2020-01-01 00:00:00");
    EXPECT_DOUBLE_EQ(kline0.OpenPrice, 7000.0);
    EXPECT_DOUBLE_EQ(kline0.ClosePrice, 7020.0);
    EXPECT_DOUBLE_EQ(kline0.Volume, 100.0);
    EXPECT_EQ(kline0.TradeCount, 50);
}
