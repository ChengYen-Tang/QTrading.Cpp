#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <Exchanges/BinanceSimulator/DataProvider/FundingRateData.hpp>

static const char* test_csv_filename = "test_funding_rate_data.csv";

/// @brief Test fixture for FundingRateData tests.
///        Creates a temporary CSV file before each test and removes it after.
class FundingRateDataTests : public ::testing::Test {
protected:
    void SetUp() override {
        boost::filesystem::ofstream ofs(test_csv_filename);
        ofs << "FundingTime,Rate,MarkPrice\n";
        ofs << "1733497260000,0.0001,7001.5\n";
        ofs << "1733497320000,-0.0002,\n";
        ofs.close();
    }

    void TearDown() override {
        boost::filesystem::remove(test_csv_filename);
    }
};

TEST_F(FundingRateDataTests, LoadCsvAndCheckCount) {
    FundingRateData fd("BTCUSDT", test_csv_filename);
    EXPECT_EQ(fd.get_count(), 2u);
}

TEST_F(FundingRateDataTests, GetLatestFunding) {
    FundingRateData fd("BTCUSDT", test_csv_filename);
    const auto& latest = fd.get_latest();
    EXPECT_EQ(latest.FundingTime, 1733497320000u);
    EXPECT_DOUBLE_EQ(latest.Rate, -0.0002);
    EXPECT_FALSE(latest.MarkPrice.has_value());
}

TEST_F(FundingRateDataTests, GetFundingOutOfRange) {
    FundingRateData fd("BTCUSDT", test_csv_filename);
    EXPECT_THROW(fd.get_funding(2), std::out_of_range);
}

TEST_F(FundingRateDataTests, IteratorTraversal) {
    FundingRateData fd("BTCUSDT", test_csv_filename);
    std::vector<uint64_t> expected = { 1733497260000u, 1733497320000u };
    size_t index = 0;
    for (const auto& row : fd) {
        ASSERT_LT(index, expected.size());
        EXPECT_EQ(row.FundingTime, expected[index]);
        index++;
    }
    EXPECT_EQ(index, fd.get_count());
}
