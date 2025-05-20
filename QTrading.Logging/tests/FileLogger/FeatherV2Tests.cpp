#include "FileLogger/FeatherV2.hpp"
#include <gtest/gtest.h>
#include <arrow/io/api.h>
#include <arrow/ipc/api.h>
#include <arrow/table.h>

#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

using namespace QTrading::Log;
namespace bfs = boost::filesystem;

/// @brief Delete the "logs" directory and all its contents.
/// @details Ensures a clean state before or after tests run.
static void ClearLogs() {
    bfs::remove_all("logs");
}

/// @brief Load an Arrow Table from an IPC file.
/// @param path The filesystem path to the Arrow IPC file (e.g., "logs/Simple.arrow").
/// @return A shared pointer to the loaded arrow::Table.
/// @throws std::runtime_error if the file cannot be opened or parsed.
static std::shared_ptr<arrow::Table> ReadTable(const std::string& path) {
    auto infile_res = arrow::io::ReadableFile::Open(path);
    if (!infile_res.ok())
        throw std::runtime_error(infile_res.status().message());
    auto infile = infile_res.ValueUnsafe();

    auto reader_res = arrow::ipc::RecordBatchFileReader::Open(infile);
    if (!reader_res.ok())
        throw std::runtime_error(reader_res.status().message());
    auto reader = reader_res.ValueUnsafe();

    std::vector<std::shared_ptr<arrow::RecordBatch>> batches;
    for (int i = 0; i < reader->num_record_batches(); ++i) {
        auto rb_res = reader->ReadRecordBatch(i);
        if (!rb_res.ok())
            throw std::runtime_error(rb_res.status().message());
        batches.push_back(rb_res.ValueUnsafe());
    }

    auto tbl_res = arrow::Table::FromRecordBatches(batches);
    if (!tbl_res.ok())
        throw std::runtime_error(tbl_res.status().message());
    return tbl_res.ValueUnsafe();
}

/// @brief Simple log payload with an integer and a string.
/// @details Used for testing single-module logging.
struct SimpleLog {
    int a;              ///< Integer field to log.
    std::string b;      ///< String field to log.
};

/// @brief Initialize and register the "Simple" module with the logger.
/// @param logger Unique pointer to the FeatherV2 logger instance.
/// @details Defines schema (timestamp, a, b) and serializer for SimpleLog.
static void InitSimpleModule(std::unique_ptr<FeatherV2> &logger) {
    auto schema = arrow::schema({
        arrow::field("ts", arrow::uint64()),
        arrow::field("a",  arrow::int32()),
        arrow::field("b",  arrow::utf8())
        });
    Serializer ser = [](const void* src, arrow::RecordBatchBuilder& bld) {
        auto p = static_cast<const SimpleLog*>(src);
        bld.GetFieldAs<arrow::Int32Builder>(1)->Append(p->a);
        bld.GetFieldAs<arrow::StringBuilder>(2)->Append(p->b);
        };
    logger->RegisterModule("Simple", schema, ser);
}

/// @brief Simple log payload with a single double field.
/// @details Used for testing multi-module logging.
struct OtherLog {
    double x;
};

/// @brief Initialize and register the "Other" module with the logger.
/// @param logger Unique pointer to the FeatherV2 logger instance.
/// @details Defines schema (timestamp, x) and serializer for OtherLog.
static void InitOtherModule(std::unique_ptr<FeatherV2> &logger) {
    auto schema = arrow::schema({
        arrow::field("ts", arrow::uint64()),
        arrow::field("x",  arrow::float64())
        });
    Serializer ser = [](const void* src, arrow::RecordBatchBuilder& bld) {
        auto p = static_cast<const OtherLog*>(src);
        bld.GetFieldAs<arrow::DoubleBuilder>(1)->Append(p->x);
        };
    logger->RegisterModule("Other", schema, ser);
}


/// @brief Test fixture for FeatherV2 logger tests.
/// @details Sets up and tears down a logger instance for each test.
class LoggerTest : public ::testing::Test {
protected:
	std::unique_ptr<FeatherV2> logger;

    /// @brief Create a new FeatherV2 logger before each test.
    void SetUp() override {
		logger = std::make_unique<FeatherV2>("logs");
    }

    /// @brief Stop the logger and clear logs after each test.
    void TearDown() override {
        logger->Stop();
        ClearLogs();
    }
};

/// @brief Verify that a single record is written correctly.
/// @details Registers the "Simple" module, logs one SimpleLog, and
/// checks that the resulting Arrow file has the correct schema and values.
TEST_F(LoggerTest, SingleRecord) {
    logger->Start();
    InitSimpleModule(logger);

    QTrading::Utils::GlobalTimestamp.store(5555);

    auto obj = std::make_shared<SimpleLog>(SimpleLog{ 42, "foo" });
    logger->Log("Simple", obj);

    logger->Stop();

    ASSERT_TRUE(bfs::exists("logs/Simple.arrow"));

    auto tbl = ReadTable("logs/Simple.arrow");
    EXPECT_EQ(tbl->num_rows(), 1);
    EXPECT_EQ(tbl->num_columns(), 3);

    auto ts_arr = std::static_pointer_cast<arrow::UInt64Array>(tbl->column(0)->chunk(0));
    EXPECT_EQ(ts_arr->Value(0), 5555ULL);

    auto a_arr = std::static_pointer_cast<arrow::Int32Array>(tbl->column(1)->chunk(0));
    EXPECT_EQ(a_arr->Value(0), 42);

    auto b_arr = std::static_pointer_cast<arrow::StringArray>(tbl->column(2)->chunk(0));
    EXPECT_EQ(b_arr->GetString(0), "foo");
}

/// @brief Ensure registering the same module twice throws an exception.
/// @details Calls InitSimpleModule twice on the same logger.
TEST_F(LoggerTest, DuplicateRegisterThrows) {
    logger->Start();
    InitSimpleModule(logger);
    EXPECT_THROW(InitSimpleModule(logger), std::runtime_error);
    logger->Stop();
}

/// @brief Test concurrent logging from multiple threads.
/// @details Spawns several threads, each logging multiple SimpleLog entries,
/// then verifies the total row count matches the expected number.
TEST_F(LoggerTest, MultiThreadLogging) {
    logger->Start();
    InitSimpleModule(logger);

    const int perThread = 200;
    const int nThreads = 4;
    std::vector<boost::thread> threads;

    for (int t = 0; t < nThreads; ++t) {
        threads.emplace_back([this, perThread]() {
            for (int i = 0; i < perThread; ++i) {
                QTrading::Utils::GlobalTimestamp.store(1000 + i);
                auto obj = std::make_shared<SimpleLog>(SimpleLog{ i, "x" });
                logger->Log("Simple", obj);
            }
            });
    }
    for (auto& th : threads) th.join();

    logger->Stop();

    auto tbl = ReadTable("logs/Simple.arrow");
    EXPECT_EQ(tbl->num_rows(), perThread * nThreads);
}

/// @brief Verify logging to multiple modules works correctly.
/// @details Registers "Simple" and "Other", logs to each, and checks
/// that both Arrow files exist with the correct timestamps.
TEST_F(LoggerTest, MultiModule) {
    logger->Start();
    InitSimpleModule(logger);
    InitOtherModule(logger);

    QTrading::Utils::GlobalTimestamp.store(1111);
    logger->Log("Simple", std::make_shared<SimpleLog>(SimpleLog{ 1, "a" }));

    QTrading::Utils::GlobalTimestamp.store(2222);
    logger->Log("Other", std::make_shared<OtherLog>(OtherLog{ 3.14 }));

    logger->Stop();

    ASSERT_TRUE(bfs::exists("logs/Simple.arrow"));
    ASSERT_TRUE(bfs::exists("logs/Other.arrow"));

    auto tbl1 = ReadTable("logs/Simple.arrow");
    EXPECT_EQ(tbl1->num_rows(), 1);
    auto ts1 = std::static_pointer_cast<arrow::UInt64Array>(tbl1->column(0)->chunk(0));
    EXPECT_EQ(ts1->Value(0), 1111ULL);

    auto tbl2 = ReadTable("logs/Other.arrow");
    EXPECT_EQ(tbl2->num_rows(), 1);
    auto ts2 = std::static_pointer_cast<arrow::UInt64Array>(tbl2->column(0)->chunk(0));
    EXPECT_EQ(ts2->Value(0), 2222ULL);
    auto x_arr = std::static_pointer_cast<arrow::DoubleArray>(tbl2->column(1)->chunk(0));
    EXPECT_DOUBLE_EQ(x_arr->Value(0), 3.14);
}
