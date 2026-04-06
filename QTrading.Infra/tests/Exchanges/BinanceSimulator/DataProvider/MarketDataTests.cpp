#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <Data/Binance/MarketData.hpp>

static const char* test_csv_filename = "test_kline_data.csv";

/// @brief Test fixture for MarketData tests.
///        Creates a temporary CSV file before each test and removes it after.
class MarketDataTests : public ::testing::Test {
protected:
    /// @brief Write header and two sample Kline rows to CSV.
    void SetUp() override {
        boost::filesystem::ofstream ofs(test_csv_filename);
        ofs << "OpenTime,OpenPrice,HighPrice,LowPrice,ClosePrice,Volume,CloseTime,QuoteVolume,TradeCount,TakerBuyBaseVolume,TakerBuyQuoteVolume\n";
        ofs << "1733497260000,7000,7050,6950,7020,100,1733497319999,700000,50,20,140000\n";
        ofs << "1733497320000,7020,7100,7000,7050,200,1733497379999,1400000,80,40,280000\n";
        ofs.close();
    }

    /// @brief Remove the temporary CSV file.
    void TearDown() override {
        boost::filesystem::remove(test_csv_filename);
    }
};

/// @brief Verifies that loading the CSV yields exactly 2 klines.
TEST_F(MarketDataTests, LoadCsvAndCheckCount) {
    MarketData md("BTCUSDT", test_csv_filename);
    EXPECT_EQ(md.get_klines_count(), 2u);
}

/// @brief Verifies that get_latest_kline returns the second row correctly.
TEST_F(MarketDataTests, GetLatestKline) {
    MarketData md("BTCUSDT", test_csv_filename);
    auto latest = md.get_latest_kline();
    EXPECT_EQ(latest.Timestamp, 1733497320000);
    EXPECT_DOUBLE_EQ(latest.OpenPrice, 7020.0);
    EXPECT_DOUBLE_EQ(latest.ClosePrice, 7050.0);
    EXPECT_DOUBLE_EQ(latest.Volume, 200.0);
    EXPECT_EQ(latest.TradeCount, 80);
}


/// @brief Verifies that accessing out-of-range index throws std::out_of_range.
TEST_F(MarketDataTests, GetKlineOutOfRange) {
    MarketData md("BTCUSDT", test_csv_filename);
    EXPECT_THROW(md.get_kline(2), std::out_of_range);
}

/// @brief Verifies that the first kline matches expected values.
TEST_F(MarketDataTests, GetSymbolAndFirstKline) {
    MarketData md("BTCUSDT", test_csv_filename);
    const auto& kline0 = md.get_kline(0);
    EXPECT_EQ(kline0.Timestamp, 1733497260000);
    EXPECT_DOUBLE_EQ(kline0.OpenPrice, 7000.0);
    EXPECT_DOUBLE_EQ(kline0.ClosePrice, 7020.0);
    EXPECT_DOUBLE_EQ(kline0.Volume, 100.0);
    EXPECT_EQ(kline0.TradeCount, 50);
}

/// @brief Verifies range-based for iteration over all klines.
TEST_F(MarketDataTests, IteratorTraversal) {
    MarketData md("BTCUSDT", test_csv_filename);
    std::vector<long long> expectedTimestamps = { 1733497260000, 1733497320000 };
    size_t index = 0;
    for (const auto& kline : md) {
        ASSERT_LT(index, expectedTimestamps.size());
        EXPECT_EQ(kline.Timestamp, expectedTimestamps[index]);
        index++;
    }
    EXPECT_EQ(index, md.get_klines_count());
}

/// @brief Verifies manual iterator begin(), ++, and end() behavior.
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

/// @brief Verifies const_iterator traversal via cbegin() and cend().
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

/// @brief Verifies compact 6-column CSV format can be parsed for mark/index klines.
TEST_F(MarketDataTests, LoadCompactSixColumnCsv)
{
    const char* compact_csv = "test_kline_data_compact.csv";
    {
        boost::filesystem::ofstream ofs(compact_csv);
        ofs << "OpenTime,OpenPrice,HighPrice,LowPrice,ClosePrice,CloseTime\n";
        ofs << "1733497260000,7000,7050,6950,7020,1733497319999\n";
        ofs << "1733497320000,7020,7100,7000,7050,1733497379999\n";
    }

    MarketData md("BTCUSDT", compact_csv);
    EXPECT_EQ(md.get_klines_count(), 2u);
    const auto& latest = md.get_latest_kline();
    EXPECT_EQ(latest.Timestamp, 1733497320000);
    EXPECT_DOUBLE_EQ(latest.ClosePrice, 7050.0);
    EXPECT_DOUBLE_EQ(latest.Volume, 0.0);
    EXPECT_EQ(latest.TradeCount, 0);

    boost::filesystem::remove(compact_csv);
}

TEST_F(MarketDataTests, LowerUpperBoundByTimestamp)
{
    MarketData md("BTCUSDT", test_csv_filename);

    EXPECT_EQ(md.lower_bound_ts(1733497260000), 0u);
    EXPECT_EQ(md.upper_bound_ts(1733497260000), 1u);
    EXPECT_EQ(md.lower_bound_ts(1733497260001), 1u);
    EXPECT_EQ(md.upper_bound_ts(1733497320000), 2u);
}

TEST_F(MarketDataTests, GetLatestKlineThrowsWhenNoParsedRows)
{
    const char* header_only_csv = "test_kline_header_only.csv";
    {
        boost::filesystem::ofstream ofs(header_only_csv);
        ofs << "OpenTime,OpenPrice,HighPrice,LowPrice,ClosePrice,Volume,CloseTime,QuoteVolume,TradeCount,TakerBuyBaseVolume,TakerBuyQuoteVolume\n";
    }

    MarketData md("BTCUSDT", header_only_csv);
    EXPECT_EQ(md.get_klines_count(), 0u);
    EXPECT_THROW(md.get_latest_kline(), std::out_of_range);

    boost::filesystem::remove(header_only_csv);
}
