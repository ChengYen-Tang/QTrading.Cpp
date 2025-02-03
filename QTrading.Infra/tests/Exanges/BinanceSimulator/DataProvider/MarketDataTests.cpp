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
        ofs << "1733497260000,7000,7050,6950,7020,100,1733497319999,700000,50,20,140000\n";
        ofs << "1733497320000,7020,7100,7000,7050,200,1733497379999,1400000,80,40,280000\n";
        ofs.close();
    }

    void TearDown() override {
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
    EXPECT_EQ(latest.Timestamp, 1733497320000);
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
    EXPECT_EQ(kline0.Timestamp, 1733497260000);
    EXPECT_DOUBLE_EQ(kline0.OpenPrice, 7000.0);
    EXPECT_DOUBLE_EQ(kline0.ClosePrice, 7020.0);
    EXPECT_DOUBLE_EQ(kline0.Volume, 100.0);
    EXPECT_EQ(kline0.TradeCount, 50);
}

TEST_F(MarketDataTests, IteratorTraversal) {
    MarketData md("BTCUSDT", test_csv_filename);
    std::vector<long long> expectedTimestamps = { 1733497260000, 1733497320000 };
    size_t index = 0;
    // 使用 range-based for loop 檢查每個 kline
    for (const auto& kline : md) {
        ASSERT_LT(index, expectedTimestamps.size());
        EXPECT_EQ(kline.Timestamp, expectedTimestamps[index]);
        index++;
    }
    EXPECT_EQ(index, md.get_klines_count());
}

TEST_F(MarketDataTests, ManualIteratorUsage) {
    MarketData md("BTCUSDT", test_csv_filename);
    auto it = md.begin();
    ASSERT_NE(it, md.end());
    EXPECT_EQ(it->Timestamp, 1733497260000);
    ++it;
    ASSERT_NE(it, md.end());
    EXPECT_EQ(it->Timestamp, 1733497320000);
    ++it;
    EXPECT_EQ(it, md.end());
}

TEST_F(MarketDataTests, ConstIteratorTraversal) {
    MarketData md("BTCUSDT", test_csv_filename);
    const MarketData& c_md = md;
    size_t count = 0;
    for (auto it = c_md.cbegin(); it != c_md.cend(); ++it) {
        if (count == 0) {
            EXPECT_EQ(it->Timestamp, 1733497260000);
        }
        else if (count == 1) {
            EXPECT_EQ(it->Timestamp, 1733497320000);
        }
        count++;
    }
    EXPECT_EQ(count, md.get_klines_count());
}
