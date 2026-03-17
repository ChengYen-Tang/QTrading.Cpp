#include <algorithm>
#include <arrow/api.h>
#include <fstream>
#include <optional>
#include <tuple>

#include <gtest/gtest.h>

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "FeatherRoundTripFixture.hpp"
#include "Global.hpp"

namespace {

void WriteBinanceCsv(
    const fs::path& path,
    const std::vector<std::tuple<
        uint64_t, double, double, double, double, double,
        uint64_t, double, int, double, double>>& rows)
{
    std::ofstream file(path, std::ios::trunc);
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
    const fs::path& path,
    const std::vector<std::tuple<uint64_t, double, std::optional<double>>>& rows)
{
    std::ofstream file(path, std::ios::trunc);
    file << "FundingTime,Rate,MarkPrice\n";
    for (const auto& row : rows) {
        file << std::get<0>(row) << ','
             << std::get<1>(row) << ',';
        if (std::get<2>(row).has_value()) {
            file << *std::get<2>(row);
        }
        file << '\n';
    }
}

void ExpectSchemaExactlyMatches(
    const std::shared_ptr<arrow::Schema>& actual,
    const std::shared_ptr<arrow::Schema>& expected)
{
    ASSERT_NE(actual, nullptr);
    ASSERT_NE(expected, nullptr);
    ASSERT_EQ(actual->num_fields(), expected->num_fields());
    for (int i = 0; i < actual->num_fields(); ++i) {
        const auto& actual_field = actual->field(i);
        const auto& expected_field = expected->field(i);
        ASSERT_NE(actual_field, nullptr);
        ASSERT_NE(expected_field, nullptr);
        EXPECT_EQ(actual_field->name(), expected_field->name());
        EXPECT_EQ(actual_field->nullable(), expected_field->nullable());
        EXPECT_TRUE(actual_field->type()->Equals(expected_field->type()));
    }
}

template <typename ArrayType>
std::shared_ptr<ArrayType> ColumnAs(const std::shared_ptr<arrow::Table>& table, int index)
{
    return std::static_pointer_cast<ArrayType>(table->column(index)->chunk(0));
}

size_t CountRowsForModule(
    const std::vector<QTrading::Log::Row>& rows,
    QTrading::Log::Logger::ModuleId module_id)
{
    return static_cast<size_t>(std::count_if(rows.begin(), rows.end(), [&](const auto& row) {
        return row.module_id == module_id;
    }));
}

} // namespace

TEST_F(InfraLogFeatherRoundTripFixture, FeatherRoundTripFixtureWritesAndReadsArrowTables)
{
    RegisterRunMetadataModule();
    RegisterMarketEventModule();
    StartLogger();

    QTrading::Utils::GlobalTimestamp.store(111u);
    QTrading::Log::FileLogger::FeatherV2::RunMetadataDto meta{};
    meta.run_id = 7001u;
    meta.strategy_name = "roundtrip";
    meta.strategy_version = "1";
    meta.strategy_params = "{}";
    meta.dataset = "fixture";
    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::RunMetadata), meta));

    QTrading::Utils::GlobalTimestamp.store(222u);
    QTrading::Log::FileLogger::FeatherV2::MarketEventDto market_a{};
    market_a.run_id = 7001u;
    market_a.step_seq = 3u;
    market_a.event_seq = 0u;
    market_a.ts_local = 500u;
    market_a.symbol = "BTCUSDT";
    market_a.has_kline = true;
    market_a.close = 101.5;
    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::MarketEvent), market_a));

    QTrading::Utils::GlobalTimestamp.store(333u);
    QTrading::Log::FileLogger::FeatherV2::MarketEventDto market_b{};
    market_b.run_id = 7001u;
    market_b.step_seq = 3u;
    market_b.event_seq = 1u;
    market_b.ts_local = 501u;
    market_b.symbol = "ETHUSDT";
    market_b.has_kline = true;
    market_b.close = 202.5;
    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::MarketEvent), market_b));

    StopLogger();

    ASSERT_TRUE(fs::exists(ArrowPath("RunMetadata")));
    ASSERT_TRUE(fs::exists(ArrowPath("MarketEvent")));

    const auto run_metadata_table = ReadArrowTable("RunMetadata");
    ASSERT_NE(run_metadata_table, nullptr);
    EXPECT_EQ(run_metadata_table->num_rows(), 1);

    const auto meta_ts = std::static_pointer_cast<arrow::UInt64Array>(run_metadata_table->column(0)->chunk(0));
    const auto meta_run_id = std::static_pointer_cast<arrow::UInt64Array>(run_metadata_table->column(1)->chunk(0));
    const auto meta_strategy = std::static_pointer_cast<arrow::StringArray>(run_metadata_table->column(2)->chunk(0));
    EXPECT_EQ(meta_ts->Value(0), 111u);
    EXPECT_EQ(meta_run_id->Value(0), 7001u);
    EXPECT_EQ(meta_strategy->GetString(0), "roundtrip");

    const auto market_table = ReadArrowTable("MarketEvent");
    ASSERT_NE(market_table, nullptr);
    EXPECT_EQ(market_table->num_rows(), 2);

    const auto market_ts = std::static_pointer_cast<arrow::UInt64Array>(market_table->column(0)->chunk(0));
    const auto market_run_id = std::static_pointer_cast<arrow::UInt64Array>(market_table->column(1)->chunk(0));
    const auto market_step_seq = std::static_pointer_cast<arrow::UInt64Array>(market_table->column(2)->chunk(0));
    const auto market_event_seq = std::static_pointer_cast<arrow::UInt64Array>(market_table->column(3)->chunk(0));
    const auto market_symbol = std::static_pointer_cast<arrow::StringArray>(market_table->column(4)->chunk(0));
    EXPECT_EQ(market_ts->Value(0), 222u);
    EXPECT_EQ(market_ts->Value(1), 333u);
    EXPECT_EQ(market_run_id->Value(0), 7001u);
    EXPECT_EQ(market_run_id->Value(1), 7001u);
    EXPECT_EQ(market_step_seq->Value(0), 3u);
    EXPECT_EQ(market_step_seq->Value(1), 3u);
    EXPECT_EQ(market_event_seq->Value(0), 0u);
    EXPECT_EQ(market_event_seq->Value(1), 1u);
    EXPECT_EQ(market_symbol->GetString(0), "BTCUSDT");
    EXPECT_EQ(market_symbol->GetString(1), "ETHUSDT");
}

TEST_F(InfraLogFeatherRoundTripFixture, AccountArrowSchemaMatchesLegacyFieldsExactly)
{
    RegisterAccountModule();
    StartLogger();

    QTrading::Utils::GlobalTimestamp.store(100u);
    QTrading::dto::AccountLog account{};
    account.balance = 10.0;
    account.unreal_pnl = 1.0;
    account.equity = 11.0;
    account.perp_wallet_balance = 10.0;
    account.perp_available_balance = 9.5;
    account.perp_ledger_value = 11.0;
    account.spot_cash_balance = 20.0;
    account.spot_available_balance = 19.5;
    account.spot_inventory_value = 5.0;
    account.spot_ledger_value = 25.0;
    account.total_cash_balance = 30.0;
    account.total_ledger_value = 36.0;
    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::Account), account));

    StopLogger();

    const auto table = ReadArrowTable("Account");
    ASSERT_NE(table, nullptr);
    ExpectSchemaExactlyMatches(table->schema(), QTrading::Log::FileLogger::FeatherV2::AccountLog::Schema);
}

TEST_F(InfraLogFeatherRoundTripFixture, OrderEventArrowSchemaMatchesLegacyFieldsExactly)
{
    RegisterOrderEventModule();
    StartLogger();

    QTrading::Utils::GlobalTimestamp.store(200u);
    QTrading::Log::FileLogger::FeatherV2::OrderEventDto event{};
    event.run_id = 8200u;
    event.step_seq = 2u;
    event.event_seq = 0u;
    event.ts_local = 201u;
    event.request_id = 77u;
    event.order_id = 77;
    event.symbol = "BTCUSDT";
    event.event_type = static_cast<int32_t>(QTrading::Log::FileLogger::FeatherV2::OrderEventType::Accepted);
    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::OrderEvent), event));

    StopLogger();

    const auto table = ReadArrowTable("OrderEvent");
    ASSERT_NE(table, nullptr);
    ExpectSchemaExactlyMatches(table->schema(), QTrading::Log::FileLogger::FeatherV2::OrderEvent::Schema());
}

TEST_F(InfraLogFeatherRoundTripFixture, PositionEventArrowSchemaMatchesLegacyFieldsExactly)
{
    RegisterPositionEventModule();
    StartLogger();

    QTrading::Utils::GlobalTimestamp.store(300u);
    QTrading::Log::FileLogger::FeatherV2::PositionEventDto event{};
    event.run_id = 8300u;
    event.step_seq = 3u;
    event.event_seq = 1u;
    event.ts_local = 301u;
    event.request_id = 88u;
    event.source_order_id = 88;
    event.position_id = 19;
    event.symbol = "ETHUSDT";
    event.event_type = static_cast<int32_t>(QTrading::Log::FileLogger::FeatherV2::PositionEventType::Opened);
    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::PositionEvent), event));

    StopLogger();

    const auto table = ReadArrowTable("PositionEvent");
    ASSERT_NE(table, nullptr);
    ExpectSchemaExactlyMatches(table->schema(), QTrading::Log::FileLogger::FeatherV2::PositionEvent::Schema());
}

TEST_F(InfraLogFeatherRoundTripFixture, FundingEventArrowSchemaMatchesLegacyFieldsExactly)
{
    RegisterFundingEventModule();
    StartLogger();

    QTrading::Utils::GlobalTimestamp.store(400u);
    QTrading::Log::FileLogger::FeatherV2::FundingEventDto event{};
    event.run_id = 8400u;
    event.step_seq = 4u;
    event.event_seq = 2u;
    event.ts_local = 401u;
    event.symbol = "BTCUSDT";
    event.funding_time = 60000u;
    event.rate = 0.001;
    event.has_mark_price = true;
    event.mark_price = 100.0;
    event.quantity = 1.5;
    event.funding = 0.15;
    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::FundingEvent), event));

    StopLogger();

    const auto table = ReadArrowTable("FundingEvent");
    ASSERT_NE(table, nullptr);
    ExpectSchemaExactlyMatches(table->schema(), QTrading::Log::FileLogger::FeatherV2::FundingEvent::Schema());
}

TEST_F(InfraLogFeatherRoundTripFixture, MarketEventArrowSchemaMatchesLegacyFieldsExactly)
{
    RegisterMarketEventModule();
    StartLogger();

    QTrading::Utils::GlobalTimestamp.store(500u);
    QTrading::Log::FileLogger::FeatherV2::MarketEventDto event{};
    event.run_id = 8500u;
    event.step_seq = 5u;
    event.event_seq = 3u;
    event.ts_local = 501u;
    event.symbol = "BTCUSDT";
    event.has_kline = true;
    event.open = 100.0;
    event.high = 101.0;
    event.low = 99.0;
    event.close = 100.5;
    event.volume = 123.0;
    event.taker_buy_base_volume = 45.0;
    event.has_mark_price = true;
    event.mark_price = 100.4;
    event.has_index_price = true;
    event.index_price = 100.3;
    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::MarketEvent), event));

    StopLogger();

    const auto table = ReadArrowTable("MarketEvent");
    ASSERT_NE(table, nullptr);
    ExpectSchemaExactlyMatches(table->schema(), QTrading::Log::FileLogger::FeatherV2::MarketEvent::Schema());
}

TEST_F(InfraLogFeatherRoundTripFixture, ArrowRowCountsMatchInMemorySinkRowsAfterMultiStepReplay)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    RegisterDefaultReplayModules();
    StartLogger();

    WriteBinanceCsv(
        tmp_dir / "rowcount_trade.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 105.0, 105.0, 105.0, 105.0, 1000.0, 90000u, 1050.0, 1, 0.0, 0.0 },
            { 120000u, 106.0, 106.0, 106.0, 106.0, 1000.0, 150000u, 1060.0, 1, 0.0, 0.0 }
        });
    WriteFundingCsv(
        tmp_dir / "rowcount_funding.csv",
        { { 60000u, 0.001, 105.0 } });

    {
        BinanceExchange exchange(
            { BinanceExchange::SymbolDataset{
                "BTCUSDT",
                (tmp_dir / "rowcount_trade.csv").string(),
                (tmp_dir / "rowcount_funding.csv").string() } },
            logger,
            1000.0,
            0,
            8600u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 0.5, 90.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    struct ModuleCheck {
        QTrading::Log::LogModule module;
        const char* file_name;
    };

    const std::vector<ModuleCheck> modules = {
        { QTrading::Log::LogModule::Account, "Account" },
        { QTrading::Log::LogModule::Position, "Position" },
        { QTrading::Log::LogModule::Order, "Order" },
        { QTrading::Log::LogModule::AccountEvent, "AccountEvent" },
        { QTrading::Log::LogModule::PositionEvent, "PositionEvent" },
        { QTrading::Log::LogModule::OrderEvent, "OrderEvent" },
        { QTrading::Log::LogModule::MarketEvent, "MarketEvent" },
        { QTrading::Log::LogModule::FundingEvent, "FundingEvent" }
    };

    for (const auto& module : modules) {
        const auto table = ReadArrowTable(module.file_name);
        ASSERT_NE(table, nullptr);
        EXPECT_EQ(
            static_cast<size_t>(table->num_rows()),
            CountRowsForModule(rows(), ModuleId(module.module)))
            << module.file_name;
    }
}

TEST_F(InfraLogFeatherRoundTripFixture, ArrowFieldValuesMatchInMemoryPayloadDecodeResults)
{
    RegisterAccountModule();
    RegisterOrderEventModule();
    RegisterPositionEventModule();
    RegisterFundingEventModule();
    RegisterMarketEventModule();
    StartLogger();

    QTrading::dto::AccountLog account{};
    account.balance = 100.0;
    account.unreal_pnl = 5.0;
    account.equity = 105.0;
    account.perp_wallet_balance = 100.0;
    account.perp_available_balance = 99.0;
    account.perp_ledger_value = 105.0;
    account.spot_cash_balance = 20.0;
    account.spot_available_balance = 19.0;
    account.spot_inventory_value = 7.0;
    account.spot_ledger_value = 27.0;
    account.total_cash_balance = 120.0;
    account.total_ledger_value = 132.0;
    QTrading::Utils::GlobalTimestamp.store(1000u);
    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::Account), account));

    QTrading::Log::FileLogger::FeatherV2::OrderEventDto order_event{};
    order_event.run_id = 8700u;
    order_event.step_seq = 7u;
    order_event.event_seq = 0u;
    order_event.ts_local = 1001u;
    order_event.request_id = 42u;
    order_event.order_id = 42;
    order_event.symbol = "BTCUSDT";
    order_event.event_type = static_cast<int32_t>(QTrading::Log::FileLogger::FeatherV2::OrderEventType::Filled);
    order_event.qty = 1.5;
    order_event.exec_qty = 1.5;
    order_event.exec_price = 101.25;
    order_event.remaining_qty = 0.0;
    order_event.fee_quote_equiv = 0.15;
    QTrading::Utils::GlobalTimestamp.store(1002u);
    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::OrderEvent), order_event));

    QTrading::Log::FileLogger::FeatherV2::PositionEventDto position_event{};
    position_event.run_id = 8700u;
    position_event.step_seq = 7u;
    position_event.event_seq = 1u;
    position_event.ts_local = 1003u;
    position_event.request_id = 42u;
    position_event.source_order_id = 42;
    position_event.position_id = 9;
    position_event.symbol = "BTCUSDT";
    position_event.is_long = true;
    position_event.event_type = static_cast<int32_t>(QTrading::Log::FileLogger::FeatherV2::PositionEventType::Opened);
    position_event.qty = 1.5;
    position_event.entry_price = 101.25;
    QTrading::Utils::GlobalTimestamp.store(1004u);
    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::PositionEvent), position_event));

    QTrading::Log::FileLogger::FeatherV2::FundingEventDto funding_event{};
    funding_event.run_id = 8700u;
    funding_event.step_seq = 8u;
    funding_event.event_seq = 0u;
    funding_event.ts_local = 1005u;
    funding_event.symbol = "BTCUSDT";
    funding_event.funding_time = 60000u;
    funding_event.rate = 0.001;
    funding_event.has_mark_price = true;
    funding_event.mark_price = 100.0;
    funding_event.quantity = 1.5;
    funding_event.funding = 0.15;
    QTrading::Utils::GlobalTimestamp.store(1006u);
    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::FundingEvent), funding_event));

    QTrading::Log::FileLogger::FeatherV2::MarketEventDto market_event{};
    market_event.run_id = 8700u;
    market_event.step_seq = 8u;
    market_event.event_seq = 1u;
    market_event.ts_local = 1007u;
    market_event.symbol = "BTCUSDT";
    market_event.has_kline = true;
    market_event.open = 100.0;
    market_event.high = 102.0;
    market_event.low = 99.0;
    market_event.close = 101.0;
    market_event.volume = 456.0;
    market_event.has_mark_price = true;
    market_event.mark_price = 100.5;
    market_event.has_index_price = true;
    market_event.index_price = 100.4;
    QTrading::Utils::GlobalTimestamp.store(1008u);
    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::MarketEvent), market_event));

    StopLogger();

    const auto account_table = ReadArrowTable("Account");
    const auto order_table = ReadArrowTable("OrderEvent");
    const auto position_table = ReadArrowTable("PositionEvent");
    const auto funding_table = ReadArrowTable("FundingEvent");
    const auto market_table = ReadArrowTable("MarketEvent");

    ASSERT_EQ(CountRowsForModule(rows(), ModuleId(QTrading::Log::LogModule::Account)), 1u);
    ASSERT_EQ(CountRowsForModule(rows(), ModuleId(QTrading::Log::LogModule::OrderEvent)), 1u);
    ASSERT_EQ(CountRowsForModule(rows(), ModuleId(QTrading::Log::LogModule::PositionEvent)), 1u);
    ASSERT_EQ(CountRowsForModule(rows(), ModuleId(QTrading::Log::LogModule::FundingEvent)), 1u);
    ASSERT_EQ(CountRowsForModule(rows(), ModuleId(QTrading::Log::LogModule::MarketEvent)), 1u);

    const auto* account_row = std::find_if(rows().begin(), rows().end(), [&](const auto& row) {
        return row.module_id == ModuleId(QTrading::Log::LogModule::Account);
    })->payload.get();
    const auto* order_row = std::find_if(rows().begin(), rows().end(), [&](const auto& row) {
        return row.module_id == ModuleId(QTrading::Log::LogModule::OrderEvent);
    })->payload.get();
    const auto* position_row = std::find_if(rows().begin(), rows().end(), [&](const auto& row) {
        return row.module_id == ModuleId(QTrading::Log::LogModule::PositionEvent);
    })->payload.get();
    const auto* funding_row = std::find_if(rows().begin(), rows().end(), [&](const auto& row) {
        return row.module_id == ModuleId(QTrading::Log::LogModule::FundingEvent);
    })->payload.get();
    const auto* market_row = std::find_if(rows().begin(), rows().end(), [&](const auto& row) {
        return row.module_id == ModuleId(QTrading::Log::LogModule::MarketEvent);
    })->payload.get();

    ASSERT_NE(account_row, nullptr);
    ASSERT_NE(order_row, nullptr);
    ASSERT_NE(position_row, nullptr);
    ASSERT_NE(funding_row, nullptr);
    ASSERT_NE(market_row, nullptr);

    const auto* account_payload = static_cast<const QTrading::dto::AccountLog*>(account_row);
    const auto* order_payload = static_cast<const QTrading::Log::FileLogger::FeatherV2::OrderEventDto*>(order_row);
    const auto* position_payload = static_cast<const QTrading::Log::FileLogger::FeatherV2::PositionEventDto*>(position_row);
    const auto* funding_payload = static_cast<const QTrading::Log::FileLogger::FeatherV2::FundingEventDto*>(funding_row);
    const auto* market_payload = static_cast<const QTrading::Log::FileLogger::FeatherV2::MarketEventDto*>(market_row);

    EXPECT_DOUBLE_EQ(ColumnAs<arrow::DoubleArray>(account_table, 1)->Value(0), account_payload->balance);
    EXPECT_DOUBLE_EQ(ColumnAs<arrow::DoubleArray>(account_table, 11)->Value(0), account_payload->total_cash_balance);
    EXPECT_DOUBLE_EQ(ColumnAs<arrow::DoubleArray>(account_table, 12)->Value(0), account_payload->total_ledger_value);

    EXPECT_EQ(ColumnAs<arrow::UInt64Array>(order_table, 1)->Value(0), order_payload->run_id);
    EXPECT_EQ(ColumnAs<arrow::StringArray>(order_table, 6)->GetString(0), order_payload->symbol);
    EXPECT_DOUBLE_EQ(ColumnAs<arrow::DoubleArray>(order_table, 16)->Value(0), order_payload->exec_qty);
    EXPECT_DOUBLE_EQ(ColumnAs<arrow::DoubleArray>(order_table, 25)->Value(0), order_payload->fee_quote_equiv);

    EXPECT_EQ(ColumnAs<arrow::UInt64Array>(position_table, 1)->Value(0), position_payload->run_id);
    EXPECT_EQ(ColumnAs<arrow::StringArray>(position_table, 7)->GetString(0), position_payload->symbol);
    EXPECT_TRUE(ColumnAs<arrow::BooleanArray>(position_table, 9)->Value(0) == position_payload->is_long);
    EXPECT_DOUBLE_EQ(ColumnAs<arrow::DoubleArray>(position_table, 11)->Value(0), position_payload->qty);

    EXPECT_EQ(ColumnAs<arrow::UInt64Array>(funding_table, 1)->Value(0), funding_payload->run_id);
    EXPECT_EQ(ColumnAs<arrow::StringArray>(funding_table, 4)->GetString(0), funding_payload->symbol);
    EXPECT_EQ(ColumnAs<arrow::UInt64Array>(funding_table, 6)->Value(0), funding_payload->funding_time);
    EXPECT_DOUBLE_EQ(ColumnAs<arrow::DoubleArray>(funding_table, 15)->Value(0), funding_payload->funding);

    EXPECT_EQ(ColumnAs<arrow::UInt64Array>(market_table, 1)->Value(0), market_payload->run_id);
    EXPECT_EQ(ColumnAs<arrow::StringArray>(market_table, 4)->GetString(0), market_payload->symbol);
    EXPECT_DOUBLE_EQ(ColumnAs<arrow::DoubleArray>(market_table, 9)->Value(0), market_payload->close);
    EXPECT_DOUBLE_EQ(ColumnAs<arrow::DoubleArray>(market_table, 16)->Value(0), market_payload->index_price);
}
