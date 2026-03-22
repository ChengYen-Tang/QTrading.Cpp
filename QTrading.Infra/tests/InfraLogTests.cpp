#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Global.hpp"
#include "BaselineSemanticsInputPinning.hpp"
#include "InfraLogTestFixture.hpp"

namespace {

struct InfraLogFixtureAccess : InfraLogTestFixture {
    using InfraLogTestFixture::BuildLegacyLogContractSnapshot;
    using InfraLogTestFixture::RegisterDefaultModules;
};

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

class ScopedEnvVar {
public:
    ScopedEnvVar(const char* key, const char* value)
        : key_(key)
    {
        if (const char* current = std::getenv(key_)) {
            had_original_ = true;
            original_value_ = current;
        }
        Set(value);
    }

    ~ScopedEnvVar()
    {
        if (had_original_) {
            Set(original_value_.c_str());
        }
        else {
            Set("");
        }
    }

private:
    void Set(const char* value)
    {
#ifdef _WIN32
        _putenv_s(key_, value);
#else
        if (value && value[0] != '\0') {
            setenv(key_, value, 1);
        }
        else {
            unsetenv(key_);
        }
#endif
    }

    const char* key_;
    bool had_original_ = false;
    std::string original_value_;
};

QTrading::dto::AccountLog MakeAccountSnapshot(double balance)
{
    QTrading::dto::AccountLog payload{};
    payload.balance = balance;
    payload.unreal_pnl = balance / 10.0;
    payload.equity = balance + payload.unreal_pnl;
    payload.perp_wallet_balance = balance;
    payload.perp_available_balance = balance - 5.0;
    payload.perp_ledger_value = balance + 10.0;
    payload.spot_cash_balance = 20.0;
    payload.spot_available_balance = 15.0;
    payload.spot_inventory_value = 25.0;
    payload.spot_ledger_value = 40.0;
    payload.total_cash_balance = payload.perp_wallet_balance + payload.spot_cash_balance;
    payload.total_ledger_value = payload.perp_ledger_value + payload.spot_ledger_value;
    return payload;
}

QTrading::dto::Position MakePositionSnapshot()
{
    QTrading::dto::Position payload{};
    payload.id = 77;
    payload.order_id = 901;
    payload.symbol = "BTCUSDT";
    payload.quantity = 0.25;
    payload.entry_price = 42000.0;
    payload.is_long = true;
    payload.unrealized_pnl = 125.5;
    payload.notional = 10500.0;
    payload.initial_margin = 350.0;
    payload.maintenance_margin = 100.0;
    payload.fee = 1.5;
    payload.leverage = 10.0;
    payload.fee_rate = 0.0004;
    payload.instrument_type = QTrading::Dto::Trading::InstrumentType::Perp;
    return payload;
}

QTrading::dto::Order MakeOrderSnapshot()
{
    QTrading::dto::Order payload{};
    payload.id = 501;
    payload.symbol = "ETHUSDT";
    payload.quantity = 2.5;
    payload.price = 3100.0;
    payload.side = QTrading::Dto::Trading::OrderSide::Sell;
    payload.position_side = QTrading::Dto::Trading::PositionSide::Both;
    payload.reduce_only = true;
    payload.closing_position_id = 77;
    payload.instrument_type = QTrading::Dto::Trading::InstrumentType::Perp;
    payload.client_order_id = "order-snapshot";
    payload.close_position = false;
    payload.quote_order_qty = 0.0;
    return payload;
}

std::vector<QTrading::dto::Position> SortPositionsForComparison(std::vector<QTrading::dto::Position> positions)
{
    std::sort(positions.begin(), positions.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.id < rhs.id;
    });
    return positions;
}

std::vector<QTrading::dto::Order> SortOrdersForComparison(std::vector<QTrading::dto::Order> orders)
{
    std::sort(orders.begin(), orders.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.id < rhs.id;
    });
    return orders;
}

bool PositionsEqualForComparison(
    const std::vector<QTrading::dto::Position>& lhs,
    const std::vector<QTrading::dto::Position>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i].id != rhs[i].id ||
            lhs[i].order_id != rhs[i].order_id ||
            lhs[i].symbol != rhs[i].symbol ||
            lhs[i].quantity != rhs[i].quantity ||
            lhs[i].entry_price != rhs[i].entry_price ||
            lhs[i].is_long != rhs[i].is_long ||
            lhs[i].unrealized_pnl != rhs[i].unrealized_pnl ||
            lhs[i].notional != rhs[i].notional ||
            lhs[i].initial_margin != rhs[i].initial_margin ||
            lhs[i].maintenance_margin != rhs[i].maintenance_margin ||
            lhs[i].fee != rhs[i].fee ||
            lhs[i].leverage != rhs[i].leverage ||
            lhs[i].fee_rate != rhs[i].fee_rate ||
            lhs[i].instrument_type != rhs[i].instrument_type) {
            return false;
        }
    }
    return true;
}

bool OrdersEqualForComparison(
    const std::vector<QTrading::dto::Order>& lhs,
    const std::vector<QTrading::dto::Order>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i].id != rhs[i].id ||
            lhs[i].symbol != rhs[i].symbol ||
            lhs[i].quantity != rhs[i].quantity ||
            lhs[i].price != rhs[i].price ||
            lhs[i].side != rhs[i].side ||
            lhs[i].position_side != rhs[i].position_side ||
            lhs[i].reduce_only != rhs[i].reduce_only ||
            lhs[i].closing_position_id != rhs[i].closing_position_id ||
            lhs[i].instrument_type != rhs[i].instrument_type ||
            lhs[i].client_order_id != rhs[i].client_order_id ||
            lhs[i].stp_mode != rhs[i].stp_mode ||
            lhs[i].close_position != rhs[i].close_position ||
            std::abs(lhs[i].quote_order_qty - rhs[i].quote_order_qty) > 1e-12 ||
            lhs[i].one_way_reverse != rhs[i].one_way_reverse) {
            return false;
        }
    }
    return true;
}

QTrading::Log::FileLogger::FeatherV2::RunMetadataDto MakeRunMetadata(
    uint64_t run_id,
    const char* strategy_name,
    const char* dataset)
{
    QTrading::Log::FileLogger::FeatherV2::RunMetadataDto payload{};
    payload.run_id = run_id;
    payload.strategy_name = strategy_name;
    payload.strategy_version = "1";
    payload.strategy_params = "{}";
    payload.dataset = dataset;
    return payload;
}

QTrading::Log::FileLogger::FeatherV2::MarketEventDto MakeMarketEvent(
    uint64_t run_id,
    uint64_t step_seq,
    uint64_t event_seq,
    uint64_t ts_local,
    const char* symbol,
    double close)
{
    QTrading::Log::FileLogger::FeatherV2::MarketEventDto payload{};
    payload.run_id = run_id;
    payload.step_seq = step_seq;
    payload.event_seq = event_seq;
    payload.ts_local = ts_local;
    payload.symbol = symbol;
    payload.has_kline = true;
    payload.close = close;
    return payload;
}

class DeferredFlushSink final : public QTrading::Log::ILogSink {
public:
    void RegisterModule(
        QTrading::Log::Logger::ModuleId,
        const std::string&,
        const std::shared_ptr<arrow::Schema>&,
        const QTrading::Log::Serializer&) override
    {
    }

    uint64_t WriteRow(const QTrading::Log::Row& row) override
    {
        pending_rows_.push_back(row);
        return 0;
    }

    uint64_t Flush() override
    {
        flushed_rows_.insert(flushed_rows_.end(), pending_rows_.begin(), pending_rows_.end());
        const auto flushed_count = static_cast<uint64_t>(pending_rows_.size());
        pending_rows_.clear();
        return flushed_count > 0 ? 1u : 0u;
    }

    void Close() override
    {
        closed_ = true;
    }

    const std::vector<QTrading::Log::Row>& rows() const
    {
        return flushed_rows_;
    }

    bool closed() const
    {
        return closed_;
    }

private:
    std::vector<QTrading::Log::Row> pending_rows_;
    std::vector<QTrading::Log::Row> flushed_rows_;
    bool closed_ = false;
};

class DeferredFlushSinkInjection {
public:
    std::unique_ptr<QTrading::Log::ILogSink> CreateSink()
    {
        auto sink = std::make_unique<DeferredFlushSink>();
        sink_ = sink.get();
        return sink;
    }

    const std::vector<QTrading::Log::Row>& rows() const
    {
        return sink_->rows();
    }

    bool closed() const
    {
        return sink_ && sink_->closed();
    }

private:
    DeferredFlushSink* sink_ = nullptr;
};

struct CapturedModuleRegistration {
    QTrading::Log::Logger::ModuleId module_id{ QTrading::Log::Logger::kInvalidModuleId };
    std::shared_ptr<arrow::Schema> schema;
    QTrading::Log::Serializer serializer;
};

class ModuleRegistrationCaptureSink final : public QTrading::Log::ILogSink {
public:
    void RegisterModule(
        QTrading::Log::Logger::ModuleId module_id,
        const std::string& module,
        const std::shared_ptr<arrow::Schema>& schema,
        const QTrading::Log::Serializer& serializer) override
    {
        registrations_[module] = CapturedModuleRegistration{
            module_id,
            schema,
            serializer
        };
    }

    uint64_t WriteRow(const QTrading::Log::Row&) override
    {
        return 0;
    }

    uint64_t Flush() override
    {
        return 0;
    }

    void Close() override
    {
    }

    const CapturedModuleRegistration* Find(const std::string& module) const
    {
        const auto it = registrations_.find(module);
        return it == registrations_.end() ? nullptr : &it->second;
    }

private:
    std::unordered_map<std::string, CapturedModuleRegistration> registrations_;
};

class ModuleRegistrationCaptureSinkInjection {
public:
    std::unique_ptr<QTrading::Log::ILogSink> CreateSink()
    {
        auto sink = std::make_unique<ModuleRegistrationCaptureSink>();
        sink_ = sink.get();
        return sink;
    }

    const CapturedModuleRegistration* Find(const std::string& module) const
    {
        return sink_ ? sink_->Find(module) : nullptr;
    }

private:
    ModuleRegistrationCaptureSink* sink_ = nullptr;
};

std::vector<LegacyLogContractSnapshot> CaptureCriticalOnlySequenceWithLoggerMode(
    const fs::path& dir,
    bool use_debug_channel)
{
    auto local_logger = std::make_shared<QTrading::Log::SinkLogger>(dir.string());
    InMemorySinkInjection sink_injection;
    ModuleIdResolver resolver;
    local_logger->AddSink(sink_injection.CreateSink());
    InfraLogFixtureAccess::RegisterDefaultModules(*local_logger, resolver);

    if (use_debug_channel) {
        local_logger->StartWithDebugChannel(8u, QTrading::Utils::Queue::OverflowPolicy::DropOldest);
    }
    else {
        local_logger->Start();
    }

    QTrading::Utils::GlobalTimestamp.store(10u);
    if (!local_logger->Log(
            QTrading::Log::LogModuleToString(QTrading::Log::LogModule::RunMetadata),
            MakeRunMetadata(700u, "critical-only", "baseline"))) {
        local_logger->Stop();
        return {};
    }

    std::vector<QTrading::Log::PayloadPtr> payloads;
    payloads.emplace_back(QTrading::Log::MakePayload<QTrading::Log::FileLogger::FeatherV2::MarketEventDto>(
        MakeMarketEvent(700u, 1u, 0u, 111u, "BTCUSDT", 100.0)));
    payloads.emplace_back(QTrading::Log::MakePayload<QTrading::Log::FileLogger::FeatherV2::MarketEventDto>(
        MakeMarketEvent(700u, 1u, 1u, 111u, "ETHUSDT", 200.0)));
    if (local_logger->LogBatchAt(
            local_logger->GetModuleId(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::MarketEvent)),
            payloads.data(),
            payloads.size(),
            111u) != payloads.size()) {
        local_logger->Stop();
        return {};
    }

    QTrading::Utils::GlobalTimestamp.store(20u);
    if (!local_logger->Log(
            QTrading::Log::LogModuleToString(QTrading::Log::LogModule::RunMetadata),
            MakeRunMetadata(701u, "critical-only-after", "baseline"))) {
        local_logger->Stop();
        return {};
    }

    local_logger->Stop();

    std::vector<LegacyLogContractSnapshot> snapshots;
    for (const auto& row : sink_injection.rows()) {
        const auto snapshot = InfraLogFixtureAccess::BuildLegacyLogContractSnapshot(resolver, &row);
        if (!snapshot.has_value()) {
            return {};
        }
        snapshots.push_back(*snapshot);
    }
    return snapshots;
}

class DeterministicReplayFixture : public ::testing::Test {
protected:
    void SetUp() override
    {
        const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        tmp_dir = fs::temp_directory_path() /
            ("QTrading_DeterministicReplay_" + std::string(test_info->test_suite_name()) + "_" + test_info->name());
        fs::create_directories(tmp_dir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(tmp_dir, ec);
    }

    static LegacyLogContractSnapshot ExpectedSnapshot(
        QTrading::Log::LogModule module,
        uint64_t ts,
        uint64_t run_id = 0u,
        uint64_t step_seq = 0u,
        uint64_t event_seq = 0u,
        std::string symbol = {})
    {
        LegacyLogContractSnapshot snapshot{};
        snapshot.module_name = QTrading::Log::LogModuleToString(module);
        switch (module) {
        case QTrading::Log::LogModule::Account: snapshot.module_id = 1u; break;
        case QTrading::Log::LogModule::Position: snapshot.module_id = 2u; break;
        case QTrading::Log::LogModule::Order: snapshot.module_id = 3u; break;
        case QTrading::Log::LogModule::AccountEvent: snapshot.module_id = 4u; break;
        case QTrading::Log::LogModule::PositionEvent: snapshot.module_id = 5u; break;
        case QTrading::Log::LogModule::OrderEvent: snapshot.module_id = 6u; break;
        case QTrading::Log::LogModule::MarketEvent: snapshot.module_id = 7u; break;
        case QTrading::Log::LogModule::FundingEvent: snapshot.module_id = 8u; break;
        case QTrading::Log::LogModule::RunMetadata: snapshot.module_id = 9u; break;
        default: snapshot.module_id = QTrading::Log::Logger::kInvalidModuleId; break;
        }
        snapshot.ts = ts;
        snapshot.run_id = run_id;
        snapshot.step_seq = step_seq;
        snapshot.event_seq = event_seq;
        snapshot.symbol = std::move(symbol);
        return snapshot;
    }

    static std::shared_ptr<Account> MakeDeterministicAccount(
        double perp_initial_wallet = 1000.0,
        double spot_initial_cash = 0.0)
    {
        Account::AccountInitConfig cfg{};
        cfg.perp_initial_wallet = perp_initial_wallet;
        cfg.spot_initial_cash = spot_initial_cash;
        auto account = std::make_shared<Account>(cfg);
        account->set_intra_bar_path_mode(Account::IntraBarPathMode::MonteCarloPath);
        account->set_intra_bar_random_seed(kRandomSeed);
        account->set_intra_bar_monte_carlo_samples(kMonteCarloSamples);
        return account;
    }

    static std::vector<LegacyLogContractSnapshot> CaptureSnapshots(
        const std::vector<QTrading::Log::Row>& rows,
        const ModuleIdResolver& resolver,
        const std::vector<QTrading::Log::LogModule>& included_modules = {})
    {
        std::vector<LegacyLogContractSnapshot> snapshots;
        std::vector<QTrading::Log::Logger::ModuleId> included_ids;
        included_ids.reserve(included_modules.size());
        for (const auto module : included_modules) {
            switch (module) {
            case QTrading::Log::LogModule::Account: included_ids.push_back(1u); break;
            case QTrading::Log::LogModule::Position: included_ids.push_back(2u); break;
            case QTrading::Log::LogModule::Order: included_ids.push_back(3u); break;
            case QTrading::Log::LogModule::AccountEvent: included_ids.push_back(4u); break;
            case QTrading::Log::LogModule::PositionEvent: included_ids.push_back(5u); break;
            case QTrading::Log::LogModule::OrderEvent: included_ids.push_back(6u); break;
            case QTrading::Log::LogModule::MarketEvent: included_ids.push_back(7u); break;
            case QTrading::Log::LogModule::FundingEvent: included_ids.push_back(8u); break;
            case QTrading::Log::LogModule::RunMetadata: included_ids.push_back(9u); break;
            default: break;
            }
        }

        for (const auto& row : rows) {
            if (!included_ids.empty() &&
                std::find(included_ids.begin(), included_ids.end(), row.module_id) == included_ids.end()) {
                continue;
            }
            const auto snapshot = InfraLogFixtureAccess::BuildLegacyLogContractSnapshot(resolver, &row);
            if (!snapshot.has_value()) {
                continue;
            }
            snapshots.push_back(*snapshot);
        }
        return snapshots;
    }

    template <typename ScenarioFn>
    std::vector<LegacyLogContractSnapshot> RunGoldenScenario(
        const std::string& replay_name,
        ScenarioFn&& scenario_fn,
        const std::vector<QTrading::Log::LogModule>& included_modules = {})
    {
        const fs::path replay_dir = tmp_dir / replay_name;
        fs::create_directories(replay_dir);

        auto replay_logger = std::make_shared<QTrading::Log::SinkLogger>(replay_dir.string());
        InMemorySinkInjection sink_injection;
        ModuleIdResolver resolver;
        replay_logger->AddSink(sink_injection.CreateSink());
        InfraLogFixtureAccess::RegisterDefaultModules(*replay_logger, resolver);
        replay_logger->Start();

        QTrading::Log::FileLogger::FeatherV2::RunMetadataDto meta{};
        meta.run_id = kRunId;
        meta.strategy_name = replay_name;
        meta.strategy_version = "1";
        meta.strategy_params = "{}";
        meta.dataset = "golden";
        QTrading::Utils::GlobalTimestamp.store(0u);
        if (!replay_logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::RunMetadata), meta)) {
            ADD_FAILURE() << "failed to log golden replay metadata";
            replay_logger->Stop();
            return {};
        }

        scenario_fn(replay_dir, replay_logger);

        replay_logger->Stop();
        return CaptureSnapshots(sink_injection.rows(), resolver, included_modules);
    }

    std::vector<LegacyLogContractSnapshot> RunReplayOnce(const std::string& replay_name)
    {
        using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

        const fs::path replay_dir = tmp_dir / replay_name;
        fs::create_directories(replay_dir);
        WriteBinanceCsv(
            replay_dir / "btc.csv",
            {
                { 0u, 100.0, 101.0, 99.0, 100.5, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
                { 60000u, 101.0, 102.0, 100.0, 101.5, 1100.0, 90000u, 1100.0, 1, 0.0, 0.0 }
            });
        WriteBinanceCsv(
            replay_dir / "eth.csv",
            {
                { 0u, 200.0, 201.0, 199.0, 200.5, 2000.0, 30000u, 2000.0, 1, 0.0, 0.0 },
                { 60000u, 201.0, 202.0, 200.0, 201.5, 2100.0, 90000u, 2100.0, 1, 0.0, 0.0 }
            });

        auto replay_logger = std::make_shared<QTrading::Log::SinkLogger>(replay_dir.string());
        InMemorySinkInjection sink_injection;
        ModuleIdResolver resolver;
        replay_logger->AddSink(sink_injection.CreateSink());
        InfraLogFixtureAccess::RegisterDefaultModules(*replay_logger, resolver);
        replay_logger->Start();

        QTrading::Log::FileLogger::FeatherV2::RunMetadataDto meta{};
        meta.run_id = kRunId;
        meta.strategy_name = "deterministic-replay";
        meta.strategy_version = "1";
        meta.strategy_params = "{}";
        meta.dataset = "fixed-csv";
        QTrading::Utils::GlobalTimestamp.store(0u);
        if (!replay_logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::RunMetadata), meta)) {
            ADD_FAILURE() << "failed to log deterministic run metadata";
            replay_logger->Stop();
            return {};
        }

        Account::AccountInitConfig cfg{};
        cfg.perp_initial_wallet = 1000.0;
        auto account = std::make_shared<Account>(cfg);
        account->set_intra_bar_path_mode(Account::IntraBarPathMode::MonteCarloPath);
        account->set_intra_bar_random_seed(kRandomSeed);
        account->set_intra_bar_monte_carlo_samples(kMonteCarloSamples);

        {
            BinanceExchange exchange(
                {
                    { "BTCUSDT", (replay_dir / "btc.csv").string() },
                    { "ETHUSDT", (replay_dir / "eth.csv").string() }
                },
                replay_logger,
                account,
                kRunId);
            auto market_channel = exchange.get_market_channel();

            while (exchange.step()) {
                const auto dto = market_channel->Receive();
                if (!dto.has_value()) {
                    ADD_FAILURE() << "deterministic replay expected market dto";
                    replay_logger->Stop();
                    return {};
                }
            }
        }

        replay_logger->Stop();

        std::vector<LegacyLogContractSnapshot> snapshots;
        const auto market_module_id = replay_logger->GetModuleId(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::MarketEvent));
        const auto run_metadata_module_id = replay_logger->GetModuleId(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::RunMetadata));
        for (const auto& row : sink_injection.rows()) {
            if (row.module_id != market_module_id && row.module_id != run_metadata_module_id) {
                continue;
            }
            const auto snapshot = InfraLogFixtureAccess::BuildLegacyLogContractSnapshot(resolver, &row);
            if (!snapshot.has_value()) {
                ADD_FAILURE() << "failed to build deterministic replay snapshot";
                return {};
            }
            snapshots.push_back(*snapshot);
        }
        return snapshots;
    }

    fs::path tmp_dir;

    inline static constexpr uint64_t kRunId = 515151u;
    inline static constexpr uint64_t kRandomSeed = 0x1234ABCDu;
    inline static constexpr size_t kMonteCarloSamples = 17u;
};

} // namespace

TEST_F(InfraLogTestFixture, SinkLoggerCapturesRows)
{
    constexpr uint64_t expected_run_id = 42;
    constexpr uint64_t expected_ts = 0xABCDEFull;
    QTrading::Utils::GlobalTimestamp.store(expected_ts);

    QTrading::Log::FileLogger::FeatherV2::RunMetadataDto payload{};
    payload.run_id = expected_run_id;
    payload.strategy_name = "InfraLogTests";
    payload.strategy_version = "1";
    payload.strategy_params = "{}";
    payload.dataset = "unit";

    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::RunMetadata), payload));
    StopLogger();

    ASSERT_TRUE(sink_injection.attached());
    ASSERT_EQ(rows().size(), 1u);

    const auto& row = rows().front();
    EXPECT_EQ(row.module_id, ModuleId(QTrading::Log::LogModule::RunMetadata));
    EXPECT_EQ(row.ts, expected_ts);

    const auto* logged = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::RunMetadataDto>(&row);
    ASSERT_NE(logged, nullptr);
    EXPECT_EQ(logged->run_id, expected_run_id);
    EXPECT_EQ(logged->strategy_name, "InfraLogTests");
}

TEST_F(InfraLogTestFixture, AccountSnapshotRowIsCapturedAfterLoggerStart)
{
    constexpr uint64_t expected_ts = 1100u;
    const auto expected = MakeAccountSnapshot(1234.5);
    QTrading::Utils::GlobalTimestamp.store(expected_ts);

    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::Account), expected));
    StopLogger();

    ASSERT_EQ(rows().size(), 1u);
    const auto& row = rows().front();
    EXPECT_EQ(row.module_id, ModuleId(QTrading::Log::LogModule::Account));
    EXPECT_EQ(row.ts, expected_ts);

    const auto* logged = RowPayloadCast<QTrading::dto::AccountLog>(&row);
    ASSERT_NE(logged, nullptr);
    EXPECT_DOUBLE_EQ(logged->balance, expected.balance);
    EXPECT_DOUBLE_EQ(logged->total_ledger_value, expected.total_ledger_value);
}

TEST_F(InfraLogTestFixture, PositionSnapshotRowIsCapturedAfterLoggerStart)
{
    constexpr uint64_t expected_ts = 1200u;
    const auto expected = MakePositionSnapshot();
    QTrading::Utils::GlobalTimestamp.store(expected_ts);

    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::Position), expected));
    StopLogger();

    ASSERT_EQ(rows().size(), 1u);
    const auto& row = rows().front();
    EXPECT_EQ(row.module_id, ModuleId(QTrading::Log::LogModule::Position));
    EXPECT_EQ(row.ts, expected_ts);

    const auto* logged = RowPayloadCast<QTrading::dto::Position>(&row);
    ASSERT_NE(logged, nullptr);
    EXPECT_EQ(logged->id, expected.id);
    EXPECT_EQ(logged->symbol, expected.symbol);
    EXPECT_DOUBLE_EQ(logged->quantity, expected.quantity);
}

TEST_F(InfraLogTestFixture, OrderSnapshotRowIsCapturedAfterLoggerStart)
{
    constexpr uint64_t expected_ts = 1300u;
    const auto expected = MakeOrderSnapshot();
    QTrading::Utils::GlobalTimestamp.store(expected_ts);

    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::Order), expected));
    StopLogger();

    ASSERT_EQ(rows().size(), 1u);
    const auto& row = rows().front();
    EXPECT_EQ(row.module_id, ModuleId(QTrading::Log::LogModule::Order));
    EXPECT_EQ(row.ts, expected_ts);

    const auto* logged = RowPayloadCast<QTrading::dto::Order>(&row);
    ASSERT_NE(logged, nullptr);
    EXPECT_EQ(logged->id, expected.id);
    EXPECT_EQ(logged->symbol, expected.symbol);
    EXPECT_EQ(logged->side, expected.side);
    EXPECT_TRUE(logged->reduce_only);
}

TEST_F(InfraLogTestFixture, MultiRowArrivalOrderRemainsStableWithinSingleStep)
{
    using QTrading::Log::FileLogger::FeatherV2::MarketEventDto;

    std::vector<QTrading::Log::PayloadPtr> payloads;
    payloads.emplace_back(QTrading::Log::MakePayload<MarketEventDto>(
        MakeMarketEvent(88u, 5u, 0u, 555u, "BTCUSDT", 100.0)));
    payloads.emplace_back(QTrading::Log::MakePayload<MarketEventDto>(
        MakeMarketEvent(88u, 5u, 1u, 555u, "ETHUSDT", 200.0)));
    payloads.emplace_back(QTrading::Log::MakePayload<MarketEventDto>(
        MakeMarketEvent(88u, 5u, 2u, 555u, "BNBUSDT", 300.0)));

    QTrading::Utils::GlobalTimestamp.store(999999u);
    ASSERT_EQ(
        logger->LogBatchAt(
            ModuleId(QTrading::Log::LogModule::MarketEvent),
            payloads.data(),
            payloads.size(),
            555u),
        payloads.size());
    StopLogger();

    const auto market_rows = FilterRowsByModule(QTrading::Log::LogModule::MarketEvent);
    ASSERT_EQ(market_rows.size(), 3u);
    AssertSingleStepEnvelope<MarketEventDto>(market_rows, 88u, 5u, 555u, 0u);

    const auto* first = RowPayloadCast<MarketEventDto>(market_rows[0].row);
    const auto* second = RowPayloadCast<MarketEventDto>(market_rows[1].row);
    const auto* third = RowPayloadCast<MarketEventDto>(market_rows[2].row);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(third, nullptr);
    EXPECT_EQ(first->symbol, "BTCUSDT");
    EXPECT_EQ(second->symbol, "ETHUSDT");
    EXPECT_EQ(third->symbol, "BNBUSDT");
}

TEST_F(InfraLogTestFixture, StopFlushesBufferedRowsThatWereNotFlushedYet)
{
    StopLogger();

    const fs::path local_dir = tmp_dir / "stop-flush";
    fs::create_directories(local_dir);

    auto local_logger = std::make_shared<QTrading::Log::SinkLogger>(local_dir.string());
    DeferredFlushSinkInjection deferred_sink;
    ModuleIdResolver resolver;
    local_logger->AddSink(deferred_sink.CreateSink());
    InfraLogFixtureAccess::RegisterDefaultModules(*local_logger, resolver);
    local_logger->Start();

    QTrading::Utils::GlobalTimestamp.store(1500u);
    ASSERT_TRUE(local_logger->Log(
        QTrading::Log::LogModuleToString(QTrading::Log::LogModule::RunMetadata),
        MakeRunMetadata(150u, "stop-flush-a", "buffered")));
    QTrading::Utils::GlobalTimestamp.store(1501u);
    ASSERT_TRUE(local_logger->Log(
        QTrading::Log::LogModuleToString(QTrading::Log::LogModule::RunMetadata),
        MakeRunMetadata(151u, "stop-flush-b", "buffered")));

    EXPECT_TRUE(deferred_sink.rows().empty());

    local_logger->Stop();

    ASSERT_TRUE(deferred_sink.closed());
    ASSERT_EQ(deferred_sink.rows().size(), 2u);
    EXPECT_EQ(deferred_sink.rows()[0].ts, 1500u);
    EXPECT_EQ(deferred_sink.rows()[1].ts, 1501u);
}

TEST_F(InfraLogTestFixture, DifferentModulesKeepTheirOwnModuleIdsWithoutCrossPollution)
{
    QTrading::Utils::GlobalTimestamp.store(1600u);
    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::Account), MakeAccountSnapshot(50.0)));
    QTrading::Utils::GlobalTimestamp.store(1601u);
    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::Position), MakePositionSnapshot()));
    QTrading::Utils::GlobalTimestamp.store(1602u);
    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::Order), MakeOrderSnapshot()));
    StopLogger();

    ASSERT_EQ(rows().size(), 3u);
    EXPECT_EQ(rows()[0].module_id, ModuleId(QTrading::Log::LogModule::Account));
    EXPECT_EQ(rows()[1].module_id, ModuleId(QTrading::Log::LogModule::Position));
    EXPECT_EQ(rows()[2].module_id, ModuleId(QTrading::Log::LogModule::Order));

    const auto account_module = ResolveModuleName(rows()[0].module_id);
    const auto position_module = ResolveModuleName(rows()[1].module_id);
    const auto order_module = ResolveModuleName(rows()[2].module_id);
    ASSERT_TRUE(account_module.has_value());
    ASSERT_TRUE(position_module.has_value());
    ASSERT_TRUE(order_module.has_value());
    EXPECT_EQ(*account_module, QTrading::Log::LogModuleToString(QTrading::Log::LogModule::Account));
    EXPECT_EQ(*position_module, QTrading::Log::LogModuleToString(QTrading::Log::LogModule::Position));
    EXPECT_EQ(*order_module, QTrading::Log::LogModuleToString(QTrading::Log::LogModule::Order));
}

TEST_F(InfraLogTestFixture, LogBatchAtPreservesExplicitTimestampInsteadOfGlobalTimestamp)
{
    using QTrading::Log::FileLogger::FeatherV2::MarketEventDto;

    std::vector<QTrading::Log::PayloadPtr> payloads;
    payloads.emplace_back(QTrading::Log::MakePayload<MarketEventDto>(
        MakeMarketEvent(170u, 1u, 0u, 777u, "BTCUSDT", 123.0)));
    payloads.emplace_back(QTrading::Log::MakePayload<MarketEventDto>(
        MakeMarketEvent(170u, 1u, 1u, 777u, "ETHUSDT", 456.0)));

    QTrading::Utils::GlobalTimestamp.store(99999999u);
    ASSERT_EQ(
        logger->LogBatchAt(
            ModuleId(QTrading::Log::LogModule::MarketEvent),
            payloads.data(),
            payloads.size(),
            777u),
        payloads.size());
    StopLogger();

    const auto market_rows = FilterRowsByModule(QTrading::Log::LogModule::MarketEvent);
    ASSERT_EQ(market_rows.size(), 2u);
    EXPECT_EQ(market_rows[0].row->ts, 777u);
    EXPECT_EQ(market_rows[1].row->ts, 777u);
    EXPECT_NE(market_rows[0].row->ts, QTrading::Utils::GlobalTimestamp.load());
}

TEST_F(InfraLogTestFixture, CriticalOnlyRowsKeepSameOrderWhenDebugChannelIsEnabled)
{
    StopLogger();

    const auto baseline = CaptureCriticalOnlySequenceWithLoggerMode(tmp_dir / "baseline-critical", false);
    const auto with_debug_channel = CaptureCriticalOnlySequenceWithLoggerMode(tmp_dir / "with-debug-channel", true);

    ASSERT_FALSE(baseline.empty());
    ASSERT_EQ(with_debug_channel.size(), baseline.size());
    EXPECT_EQ(with_debug_channel, baseline);
}

TEST_F(InfraLogTestFixture, InMemorySinkInjectionRowsRemainReadableAfterLoggerStop)
{
    constexpr uint64_t expected_run_id = 7;
    constexpr uint64_t expected_ts = 123456u;
    QTrading::Utils::GlobalTimestamp.store(expected_ts);

    QTrading::Log::FileLogger::FeatherV2::RunMetadataDto payload{};
    payload.run_id = expected_run_id;
    payload.strategy_name = "Case2";
    payload.strategy_version = "1";
    payload.strategy_params = "{}";
    payload.dataset = "rows-readable";

    ASSERT_TRUE(sink_injection.attached());
    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::RunMetadata), payload));

    StopLogger();

    const auto& captured_rows = rows();
    ASSERT_EQ(captured_rows.size(), 1u);

    const auto& row = captured_rows.front();
    EXPECT_EQ(row.module_id, ModuleId(QTrading::Log::LogModule::RunMetadata));
    EXPECT_EQ(row.ts, expected_ts);

    const auto* logged = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::RunMetadataDto>(&row);
    ASSERT_NE(logged, nullptr);
    EXPECT_EQ(logged->run_id, expected_run_id);
    EXPECT_EQ(logged->dataset, "rows-readable");
}

TEST_F(InfraLogTestFixture, ModuleIdResolverResolvesModuleNameFromModuleId)
{
    const auto run_metadata_module_id = ModuleId(QTrading::Log::LogModule::RunMetadata);
    ASSERT_NE(run_metadata_module_id, QTrading::Log::Logger::kInvalidModuleId);

    const auto resolved_name = ResolveModuleName(run_metadata_module_id);
    ASSERT_TRUE(resolved_name.has_value());
    EXPECT_EQ(*resolved_name, QTrading::Log::LogModuleToString(QTrading::Log::LogModule::RunMetadata));

    const auto unknown_name = ResolveModuleName(999999u);
    EXPECT_FALSE(unknown_name.has_value());
}

TEST_F(InfraLogTestFixture, RegisterDefaultModulesRegistersAllExistingModuleNames)
{
    const std::vector<QTrading::Log::LogModule> expected_modules{
        QTrading::Log::LogModule::Account,
        QTrading::Log::LogModule::Position,
        QTrading::Log::LogModule::Order,
        QTrading::Log::LogModule::AccountEvent,
        QTrading::Log::LogModule::PositionEvent,
        QTrading::Log::LogModule::OrderEvent,
        QTrading::Log::LogModule::MarketEvent,
        QTrading::Log::LogModule::FundingEvent,
        QTrading::Log::LogModule::RunMetadata
    };

    for (const auto module : expected_modules) {
        const auto module_name = QTrading::Log::LogModuleToString(module);
        const auto module_id = ModuleId(module);
        EXPECT_NE(module_id, QTrading::Log::Logger::kInvalidModuleId) << module_name;

        const auto resolved_name = ResolveModuleName(module_id);
        ASSERT_TRUE(resolved_name.has_value()) << module_name;
        EXPECT_EQ(*resolved_name, module_name);
    }
}

TEST_F(InfraLogTestFixture, RegisterDefaultModulesKeepsStableModuleIdOrder)
{
    struct ExpectedModuleId {
        QTrading::Log::LogModule module;
        QTrading::Log::Logger::ModuleId expected_id;
    };

    const std::vector<ExpectedModuleId> expected{
        { QTrading::Log::LogModule::Account, 1u },
        { QTrading::Log::LogModule::Position, 2u },
        { QTrading::Log::LogModule::Order, 3u },
        { QTrading::Log::LogModule::AccountEvent, 4u },
        { QTrading::Log::LogModule::PositionEvent, 5u },
        { QTrading::Log::LogModule::OrderEvent, 6u },
        { QTrading::Log::LogModule::MarketEvent, 7u },
        { QTrading::Log::LogModule::FundingEvent, 8u },
        { QTrading::Log::LogModule::RunMetadata, 9u }
    };

    for (const auto& entry : expected) {
        EXPECT_EQ(ModuleId(entry.module), entry.expected_id)
            << QTrading::Log::LogModuleToString(entry.module);
    }
}

TEST_F(InfraLogTestFixture, RegisterDefaultModulesKeepsExpectedSchemaAndSerializerForModuleNames)
{
    StopLogger();

    const fs::path local_dir = tmp_dir / "module-registration-capture";
    fs::create_directories(local_dir);

    QTrading::Log::SinkLogger local_logger(local_dir.string());
    ModuleIdResolver resolver;
    ModuleRegistrationCaptureSinkInjection capture_sink;
    local_logger.AddSink(capture_sink.CreateSink());
    InfraLogFixtureAccess::RegisterDefaultModules(local_logger, resolver);

    const auto* account_registration =
        capture_sink.Find(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::Account));
    const auto* position_registration =
        capture_sink.Find(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::Position));
    const auto* order_registration =
        capture_sink.Find(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::Order));

    ASSERT_NE(account_registration, nullptr);
    ASSERT_NE(position_registration, nullptr);
    ASSERT_NE(order_registration, nullptr);

    ASSERT_EQ(account_registration->schema->field(1)->name(), "balance");
    ASSERT_EQ(account_registration->schema->field(12)->name(), "total_ledger_value");
    ASSERT_EQ(position_registration->schema->field(1)->name(), "id");
    ASSERT_EQ(position_registration->schema->field(3)->name(), "symbol");
    ASSERT_EQ(order_registration->schema->field(7)->name(), "reduce_only");
    ASSERT_EQ(order_registration->schema->field(8)->name(), "closing_position_id");

    auto account_builder_result =
        arrow::RecordBatchBuilder::Make(account_registration->schema, arrow::default_memory_pool(), 1);
    ASSERT_TRUE(account_builder_result.ok()) << account_builder_result.status().ToString();
    auto account_builder = std::move(*account_builder_result);
    ASSERT_TRUE(account_builder->GetFieldAs<arrow::UInt64Builder>(0)->Append(1900u).ok());
    const auto account_payload = MakeAccountSnapshot(321.0);
    account_registration->serializer(&account_payload, *account_builder);
    auto account_batch_result = account_builder->Flush();
    ASSERT_TRUE(account_batch_result.ok()) << account_batch_result.status().ToString();
    const auto account_batch = *account_batch_result;
    ASSERT_EQ(account_batch->num_rows(), 1);
    EXPECT_DOUBLE_EQ(std::static_pointer_cast<arrow::DoubleArray>(account_batch->column(1))->Value(0), 321.0);
    EXPECT_DOUBLE_EQ(std::static_pointer_cast<arrow::DoubleArray>(account_batch->column(12))->Value(0),
        account_payload.total_ledger_value);

    auto position_builder_result =
        arrow::RecordBatchBuilder::Make(position_registration->schema, arrow::default_memory_pool(), 1);
    ASSERT_TRUE(position_builder_result.ok()) << position_builder_result.status().ToString();
    auto position_builder = std::move(*position_builder_result);
    ASSERT_TRUE(position_builder->GetFieldAs<arrow::UInt64Builder>(0)->Append(1901u).ok());
    const auto position_payload = MakePositionSnapshot();
    position_registration->serializer(&position_payload, *position_builder);
    auto position_batch_result = position_builder->Flush();
    ASSERT_TRUE(position_batch_result.ok()) << position_batch_result.status().ToString();
    const auto position_batch = *position_batch_result;
    ASSERT_EQ(position_batch->num_rows(), 1);
    EXPECT_EQ(std::static_pointer_cast<arrow::StringArray>(position_batch->column(3))->GetString(0), position_payload.symbol);
    EXPECT_DOUBLE_EQ(std::static_pointer_cast<arrow::DoubleArray>(position_batch->column(5))->Value(0), position_payload.quantity);

    auto order_builder_result =
        arrow::RecordBatchBuilder::Make(order_registration->schema, arrow::default_memory_pool(), 1);
    ASSERT_TRUE(order_builder_result.ok()) << order_builder_result.status().ToString();
    auto order_builder = std::move(*order_builder_result);
    ASSERT_TRUE(order_builder->GetFieldAs<arrow::UInt64Builder>(0)->Append(1902u).ok());
    const auto order_payload = MakeOrderSnapshot();
    order_registration->serializer(&order_payload, *order_builder);
    auto order_batch_result = order_builder->Flush();
    ASSERT_TRUE(order_batch_result.ok()) << order_batch_result.status().ToString();
    const auto order_batch = *order_batch_result;
    ASSERT_EQ(order_batch->num_rows(), 1);
    EXPECT_EQ(std::static_pointer_cast<arrow::StringArray>(order_batch->column(2))->GetString(0), order_payload.symbol);
    EXPECT_TRUE(std::static_pointer_cast<arrow::BooleanArray>(order_batch->column(7))->Value(0));
    EXPECT_EQ(std::static_pointer_cast<arrow::Int32Array>(order_batch->column(8))->Value(0), order_payload.closing_position_id);
}

TEST_F(InfraLogTestFixture, RunMetadataAppearsFirstAfterLoggerInitialization)
{
    QTrading::Utils::GlobalTimestamp.store(2200u);
    ASSERT_TRUE(logger->Log(
        QTrading::Log::LogModuleToString(QTrading::Log::LogModule::RunMetadata),
        MakeRunMetadata(220u, "session-start", "contract")));

    QTrading::Utils::GlobalTimestamp.store(2201u);
    ASSERT_TRUE(logger->Log(
        QTrading::Log::LogModuleToString(QTrading::Log::LogModule::Account),
        MakeAccountSnapshot(500.0)));

    QTrading::Utils::GlobalTimestamp.store(2202u);
    ASSERT_TRUE(logger->Log(
        QTrading::Log::LogModuleToString(QTrading::Log::LogModule::Position),
        MakePositionSnapshot()));

    StopLogger();

    const auto arrival_rows = DrainAndSortRowsByArrival();
    ASSERT_EQ(arrival_rows.size(), 3u);
    ASSERT_NE(arrival_rows[0].row, nullptr);
    EXPECT_EQ(arrival_rows[0].row->module_id, ModuleId(QTrading::Log::LogModule::RunMetadata));

    const auto* run_metadata =
        RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::RunMetadataDto>(arrival_rows[0].row);
    ASSERT_NE(run_metadata, nullptr);
    EXPECT_EQ(run_metadata->run_id, 220u);
    EXPECT_EQ(run_metadata->strategy_name, "session-start");

    EXPECT_EQ(arrival_rows[1].row->module_id, ModuleId(QTrading::Log::LogModule::Account));
    EXPECT_EQ(arrival_rows[2].row->module_id, ModuleId(QTrading::Log::LogModule::Position));
}

TEST_F(InfraLogTestFixture, StepRowsUseMarketTimestampAsTsExchangeWithinSingleStep)
{
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    WriteBinanceCsv(
        tmp_dir / "btc.csv",
        { { 0u, 100.0, 101.0, 99.0, 100.5, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });
    WriteBinanceCsv(
        tmp_dir / "eth.csv",
        { { 0u, 200.0, 201.0, 199.0, 200.5, 2000.0, 30000u, 2000.0, 1, 0.0, 0.0 } });

    constexpr uint64_t expected_run_id = 2300u;
    uint64_t step_market_ts = 0;

    {
        BinanceExchange exchange(
            {
                { "BTCUSDT", (tmp_dir / "btc.csv").string() },
                { "ETHUSDT", (tmp_dir / "eth.csv").string() }
            },
            logger,
            1000.0,
            0,
            expected_run_id);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        const auto dto = market_channel->Receive();
        ASSERT_TRUE(dto.has_value());
        ASSERT_NE(dto->get(), nullptr);
        step_market_ts = dto->get()->Timestamp;
    }

    StopLogger();

    const auto market_rows = FilterRowsByModule(QTrading::Log::LogModule::MarketEvent);
    const auto account_rows = FilterRowsByModule(QTrading::Log::LogModule::AccountEvent);
    ASSERT_EQ(step_market_ts, 0u);
    ASSERT_EQ(market_rows.size(), 2u);
    ASSERT_EQ(account_rows.size(), 1u);

    for (const auto& row_view : market_rows) {
        ASSERT_NE(row_view.row, nullptr);
        EXPECT_EQ(row_view.row->ts, step_market_ts);
    }
    EXPECT_EQ(account_rows.front().row->ts, step_market_ts);
}

TEST_F(InfraLogTestFixture, EventSeqIsMonotonicWithinSingleStepAcrossEventModules)
{
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::AccountEventDto;
    using QTrading::Log::FileLogger::FeatherV2::MarketEventDto;

    WriteBinanceCsv(
        tmp_dir / "btc.csv",
        { { 0u, 100.0, 101.0, 99.0, 100.5, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });
    WriteBinanceCsv(
        tmp_dir / "eth.csv",
        { { 0u, 200.0, 201.0, 199.0, 200.5, 2000.0, 30000u, 2000.0, 1, 0.0, 0.0 } });

    {
        BinanceExchange exchange(
            {
                { "BTCUSDT", (tmp_dir / "btc.csv").string() },
                { "ETHUSDT", (tmp_dir / "eth.csv").string() }
            },
            logger,
            1000.0,
            0,
            2400u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        const auto dto = market_channel->Receive();
        ASSERT_TRUE(dto.has_value());
    }

    StopLogger();

    std::vector<ArrivedRowView> event_rows;
    for (const auto& row_view : DrainAndSortRowsByArrival()) {
        if (!row_view.row) {
            continue;
        }
        if (row_view.row->module_id == ModuleId(QTrading::Log::LogModule::MarketEvent) ||
            row_view.row->module_id == ModuleId(QTrading::Log::LogModule::AccountEvent)) {
            event_rows.push_back(row_view);
        }
    }

    ASSERT_EQ(event_rows.size(), 3u);
    for (size_t i = 0; i < event_rows.size(); ++i) {
        ASSERT_NE(event_rows[i].row, nullptr);
        uint64_t event_seq = std::numeric_limits<uint64_t>::max();
        uint64_t step_seq = 0;

        if (event_rows[i].row->module_id == ModuleId(QTrading::Log::LogModule::MarketEvent)) {
            const auto* payload = RowPayloadCast<MarketEventDto>(event_rows[i].row);
            ASSERT_NE(payload, nullptr);
            event_seq = payload->event_seq;
            step_seq = payload->step_seq;
        }
        else {
            const auto* payload = RowPayloadCast<AccountEventDto>(event_rows[i].row);
            ASSERT_NE(payload, nullptr);
            event_seq = payload->event_seq;
            step_seq = payload->step_seq;
        }

        EXPECT_EQ(step_seq, 1u);
        EXPECT_EQ(event_seq, i);
    }
}

TEST_F(InfraLogTestFixture, StepSeqIncrementsByExactlyOnePerStep)
{
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::MarketEventDto;

    WriteBinanceCsv(
        tmp_dir / "btc.csv",
        {
            { 0u, 100.0, 101.0, 99.0, 100.5, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 101.0, 102.0, 100.0, 101.5, 1100.0, 90000u, 1100.0, 1, 0.0, 0.0 }
        });

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "btc.csv").string() } },
            logger,
            1000.0,
            0,
            2500u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto market_rows = FilterRowsByModule(QTrading::Log::LogModule::MarketEvent);
    ASSERT_EQ(market_rows.size(), 2u);

    const auto* first = RowPayloadCast<MarketEventDto>(market_rows[0].row);
    const auto* second = RowPayloadCast<MarketEventDto>(market_rows[1].row);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->step_seq, 1u);
    EXPECT_EQ(second->step_seq, 2u);
    EXPECT_EQ(second->step_seq, first->step_seq + 1u);
}

TEST_F(InfraLogTestFixture, ReplayWindowFirstStepKeepsCorrectTsExchange)
{
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::MarketEventDto;

    WriteBinanceCsv(
        tmp_dir / "btc.csv",
        {
            { 0u, 100.0, 101.0, 99.0, 100.5, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 101.0, 102.0, 100.0, 101.5, 1100.0, 90000u, 1100.0, 1, 0.0, 0.0 },
            { 120000u, 102.0, 103.0, 101.0, 102.5, 1200.0, 150000u, 1200.0, 1, 0.0, 0.0 }
        });

    ScopedEnvVar replay_start("QTR_SIM_START_TS_MS", "60000");
    ScopedEnvVar replay_end("QTR_SIM_END_TS_MS", "120000");

    uint64_t first_ts = 0;
    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "btc.csv").string() } },
            logger,
            1000.0,
            0,
            2600u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        const auto dto = market_channel->Receive();
        ASSERT_TRUE(dto.has_value());
        ASSERT_NE(dto->get(), nullptr);
        first_ts = dto->get()->Timestamp;
    }

    StopLogger();

    ASSERT_EQ(first_ts, 60000u);
    const auto market_rows = FilterRowsByModule(QTrading::Log::LogModule::MarketEvent);
    ASSERT_EQ(market_rows.size(), 1u);
    EXPECT_EQ(market_rows.front().row->ts, 60000u);

    const auto* payload = RowPayloadCast<MarketEventDto>(market_rows.front().row);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->step_seq, 1u);
}

TEST_F(InfraLogTestFixture, FundingOnlyStepProducesCorrectTsExchange)
{
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::FundingEventDto;

    WriteBinanceCsv(
        tmp_dir / "btc.csv",
        {
            { 0u, 100.0, 101.0, 99.0, 100.5, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 101.0, 102.0, 100.0, 101.5, 1100.0, 90000u, 1100.0, 1, 0.0, 0.0 }
        });
    WriteFundingCsv(
        tmp_dir / "btc_funding.csv",
        {
            { 30000u, 0.001, std::nullopt }
        });

    {
        BinanceExchange exchange(
            {
                BinanceExchange::SymbolDataset{
                    "BTCUSDT",
                    (tmp_dir / "btc.csv").string(),
                    std::optional<std::string>((tmp_dir / "btc_funding.csv").string())
                }
            },
            logger,
            1000.0,
            0,
            2700u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        ASSERT_TRUE(exchange.step());
        const auto funding_step = market_channel->Receive();
        ASSERT_TRUE(funding_step.has_value());
        ASSERT_NE(funding_step->get(), nullptr);
        EXPECT_EQ(funding_step->get()->Timestamp, 30000u);
    }

    StopLogger();

    const auto funding_rows = FilterRowsByModule(QTrading::Log::LogModule::FundingEvent);
    ASSERT_EQ(funding_rows.size(), 1u);
    EXPECT_EQ(funding_rows.front().row->ts, 30000u);

    const auto* payload = RowPayloadCast<FundingEventDto>(funding_rows.front().row);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->funding_time, 30000u);
    EXPECT_EQ(payload->step_seq, 2u);
    EXPECT_EQ(payload->skip_reason, 1);
}

TEST_F(InfraLogTestFixture, AsyncOrderAckAndOrderEventKeepSubmittedDueResolvedStepRelationship)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventDto;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventType;

    WriteBinanceCsv(
        tmp_dir / "btc.csv",
        {
            { 0u, 100.0, 101.0, 99.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 100.0, 101.0, 99.0, 100.0, 1000.0, 90000u, 1000.0, 1, 0.0, 0.0 }
        });

    BinanceExchange::AsyncOrderAck pending_ack{};
    BinanceExchange::AsyncOrderAck resolved_ack{};

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "btc.csv").string() } },
            logger,
            1000.0,
            0,
            2800u);
        exchange.set_order_latency_bars(1);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        ASSERT_TRUE(exchange.perp.place_order(
            "BTCUSDT",
            1.0,
            50.0,
            OrderSide::Buy,
            PositionSide::Both,
            false,
            "cid-step-contract"));

        auto pending_acks = exchange.drain_async_order_acks();
        ASSERT_EQ(pending_acks.size(), 1u);
        pending_ack = pending_acks.front();

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        auto resolved_acks = exchange.drain_async_order_acks();
        ASSERT_EQ(resolved_acks.size(), 1u);
        resolved_ack = resolved_acks.front();
    }

    StopLogger();

    ASSERT_EQ(pending_ack.status, BinanceExchange::AsyncOrderAck::Status::Pending);
    EXPECT_EQ(pending_ack.submitted_step, 1u);
    EXPECT_EQ(pending_ack.due_step, 2u);
    EXPECT_EQ(pending_ack.resolved_step, 0u);

    ASSERT_EQ(resolved_ack.status, BinanceExchange::AsyncOrderAck::Status::Accepted);
    EXPECT_EQ(resolved_ack.request_id, pending_ack.request_id);
    EXPECT_EQ(resolved_ack.submitted_step, pending_ack.submitted_step);
    EXPECT_EQ(resolved_ack.due_step, pending_ack.due_step);
    EXPECT_EQ(resolved_ack.resolved_step, 2u);
    EXPECT_GT(resolved_ack.resolved_step, resolved_ack.submitted_step);

    const auto order_event_rows = FilterRowsByModule(QTrading::Log::LogModule::OrderEvent);
    ASSERT_EQ(order_event_rows.size(), 1u);
    const auto* order_event = RowPayloadCast<OrderEventDto>(order_event_rows.front().row);
    ASSERT_NE(order_event, nullptr);
    EXPECT_EQ(order_event->event_type, static_cast<int32_t>(OrderEventType::Accepted));
    EXPECT_EQ(order_event->step_seq, resolved_ack.resolved_step);
    EXPECT_EQ(order_event->step_seq, resolved_ack.due_step);
    EXPECT_EQ(order_event->symbol, "BTCUSDT");
}

TEST_F(InfraLogTestFixture, FirstFillProducesAccountSnapshotAlignedWithFillStatusSnapshot)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    WriteBinanceCsv(
        tmp_dir / "btc.csv",
        { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });

    BinanceExchange::StatusSnapshot snap{};
    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "btc.csv").string() } },
            logger,
            500.0,
            0,
            2900u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 0.5, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        exchange.FillStatusSnapshot(snap);
    }

    StopLogger();

    const auto account_rows = FilterRowsByModule(QTrading::Log::LogModule::Account);
    ASSERT_EQ(account_rows.size(), 1u);
    const auto* account = RowPayloadCast<QTrading::dto::AccountLog>(account_rows.front().row);
    ASSERT_NE(account, nullptr);

    EXPECT_DOUBLE_EQ(account->balance, snap.wallet_balance);
    EXPECT_DOUBLE_EQ(account->unreal_pnl, snap.unrealized_pnl);
    EXPECT_DOUBLE_EQ(account->equity, snap.margin_balance);
    EXPECT_DOUBLE_EQ(account->perp_wallet_balance, snap.perp_wallet_balance);
    EXPECT_DOUBLE_EQ(account->perp_available_balance, snap.perp_available_balance);
    EXPECT_DOUBLE_EQ(account->perp_ledger_value, snap.perp_margin_balance);
    EXPECT_DOUBLE_EQ(account->spot_cash_balance, snap.spot_cash_balance);
    EXPECT_DOUBLE_EQ(account->spot_available_balance, snap.spot_available_balance);
    EXPECT_DOUBLE_EQ(account->spot_inventory_value, snap.spot_inventory_value);
    EXPECT_DOUBLE_EQ(account->spot_ledger_value, snap.spot_ledger_value);
    EXPECT_DOUBLE_EQ(account->total_cash_balance, snap.total_cash_balance);
    EXPECT_DOUBLE_EQ(account->total_ledger_value, snap.total_ledger_value);
}

TEST_F(InfraLogTestFixture, DualLedgerAccountSnapshotContainsStableSpotAndPerpFields)
{
    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    WriteBinanceCsv(
        tmp_dir / "btc_spot.csv",
        { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });
    WriteBinanceCsv(
        tmp_dir / "eth_perp.csv",
        { { 0u, 200.0, 200.0, 200.0, 200.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });

    Account::AccountInitConfig cfg{};
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 1000.0;

    BinanceExchange::StatusSnapshot snap{};
    {
        BinanceExchange exchange(
            {
                BinanceExchange::SymbolDataset{
                    "BTCUSDT_SPOT",
                    (tmp_dir / "btc_spot.csv").string(),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    InstrumentType::Spot },
                BinanceExchange::SymbolDataset{
                    "ETHUSDT_PERP",
                    (tmp_dir / "eth_perp.csv").string(),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    InstrumentType::Perp }
            },
            logger,
            cfg,
            3000u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.spot.place_order("BTCUSDT_SPOT", 1.0, 100.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.perp.place_order("ETHUSDT_PERP", 1.5, OrderSide::Sell));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        exchange.FillStatusSnapshot(snap);
    }

    StopLogger();

    const auto account_rows = FilterRowsByModule(QTrading::Log::LogModule::Account);
    ASSERT_EQ(account_rows.size(), 1u);
    const auto* account = RowPayloadCast<QTrading::dto::AccountLog>(account_rows.front().row);
    ASSERT_NE(account, nullptr);

    EXPECT_GT(snap.spot_inventory_value, 0.0);
    EXPECT_DOUBLE_EQ(account->perp_wallet_balance, snap.perp_wallet_balance);
    EXPECT_DOUBLE_EQ(account->perp_available_balance, snap.perp_available_balance);
    EXPECT_DOUBLE_EQ(account->perp_ledger_value, snap.perp_margin_balance);
    EXPECT_DOUBLE_EQ(account->spot_cash_balance, snap.spot_cash_balance);
    EXPECT_DOUBLE_EQ(account->spot_available_balance, snap.spot_available_balance);
    EXPECT_DOUBLE_EQ(account->spot_inventory_value, snap.spot_inventory_value);
    EXPECT_DOUBLE_EQ(account->spot_ledger_value, snap.spot_ledger_value);
    EXPECT_DOUBLE_EQ(account->total_cash_balance, snap.total_cash_balance);
    EXPECT_DOUBLE_EQ(account->total_ledger_value, snap.total_ledger_value);
    EXPECT_DOUBLE_EQ(account->spot_ledger_value, account->spot_cash_balance + account->spot_inventory_value);
    EXPECT_DOUBLE_EQ(account->total_ledger_value, account->perp_ledger_value + account->spot_ledger_value);
}

TEST_F(InfraLogTestFixture, PositionSnapshotCarriesInstrumentTypeDirectionQuantityAndEntryPrice)
{
    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    WriteBinanceCsv(
        tmp_dir / "btc_spot.csv",
        { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });
    WriteBinanceCsv(
        tmp_dir / "eth_perp.csv",
        { { 0u, 200.0, 200.0, 200.0, 200.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });

    Account::AccountInitConfig cfg{};
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 1000.0;

    {
        BinanceExchange exchange(
            {
                BinanceExchange::SymbolDataset{
                    "BTCUSDT_SPOT",
                    (tmp_dir / "btc_spot.csv").string(),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    InstrumentType::Spot },
                BinanceExchange::SymbolDataset{
                    "ETHUSDT_PERP",
                    (tmp_dir / "eth_perp.csv").string(),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    InstrumentType::Perp }
            },
            logger,
            cfg,
            3100u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.spot.place_order("BTCUSDT_SPOT", 1.0, 100.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.perp.place_order("ETHUSDT_PERP", 1.5, OrderSide::Sell));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto position_rows = FilterRowsByModule(QTrading::Log::LogModule::Position);
    ASSERT_EQ(position_rows.size(), 2u);

    const QTrading::dto::Position* spot_position = nullptr;
    const QTrading::dto::Position* perp_position = nullptr;
    for (const auto& row_view : position_rows) {
        const auto* payload = RowPayloadCast<QTrading::dto::Position>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->symbol == "BTCUSDT_SPOT") {
            spot_position = payload;
        }
        if (payload->symbol == "ETHUSDT_PERP") {
            perp_position = payload;
        }
    }

    ASSERT_NE(spot_position, nullptr);
    ASSERT_NE(perp_position, nullptr);

    EXPECT_EQ(spot_position->instrument_type, InstrumentType::Spot);
    EXPECT_TRUE(spot_position->is_long);
    EXPECT_DOUBLE_EQ(spot_position->quantity, 1.0);
    EXPECT_DOUBLE_EQ(spot_position->entry_price, 100.0);

    EXPECT_EQ(perp_position->instrument_type, InstrumentType::Perp);
    EXPECT_FALSE(perp_position->is_long);
    EXPECT_DOUBLE_EQ(perp_position->quantity, 1.5);
    EXPECT_DOUBLE_EQ(perp_position->entry_price, 200.0);
}

TEST_F(InfraLogTestFixture, OrderSnapshotCarriesInstrumentTypeReduceOnlyClosePositionAndPositionSide)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    WriteBinanceCsv(
        tmp_dir / "btc.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 100.0, 100.0, 100.0, 100.0, 1000.0, 90000u, 1000.0, 1, 0.0, 0.0 }
        });

    Account::AccountInitConfig cfg{};
    cfg.perp_initial_wallet = 1000.0;
    cfg.strict_binance_mode = false;
    auto account = std::make_shared<Account>(cfg);
    account->set_position_mode(true);
    account->set_symbol_leverage("BTCUSDT", 10.0);

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "btc.csv").string() } },
            logger,
            account,
            3200u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy, PositionSide::Long));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        ASSERT_TRUE(exchange.perp.place_order(
            "BTCUSDT",
            0.25,
            150.0,
            OrderSide::Sell,
            PositionSide::Long,
            true,
            "reduce-only-order"));
        ASSERT_TRUE(exchange.perp.place_close_position_order(
            "BTCUSDT",
            OrderSide::Sell,
            PositionSide::Long,
            160.0,
            "close-position-order"));

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto order_rows = FilterRowsByModule(QTrading::Log::LogModule::Order);
    ASSERT_EQ(order_rows.size(), 2u);

    const QTrading::dto::Order* reduce_only_order = nullptr;
    const QTrading::dto::Order* close_position_order = nullptr;
    for (const auto& row_view : order_rows) {
        const auto* payload = RowPayloadCast<QTrading::dto::Order>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->reduce_only) {
            reduce_only_order = payload;
        }
        if (payload->close_position) {
            close_position_order = payload;
        }
    }

    ASSERT_NE(reduce_only_order, nullptr);
    ASSERT_NE(close_position_order, nullptr);

    EXPECT_EQ(reduce_only_order->instrument_type, QTrading::Dto::Trading::InstrumentType::Perp);
    EXPECT_TRUE(reduce_only_order->reduce_only);
    EXPECT_FALSE(reduce_only_order->close_position);
    EXPECT_EQ(reduce_only_order->position_side, PositionSide::Long);

    EXPECT_EQ(close_position_order->instrument_type, QTrading::Dto::Trading::InstrumentType::Perp);
    EXPECT_FALSE(close_position_order->reduce_only);
    EXPECT_TRUE(close_position_order->close_position);
    EXPECT_EQ(close_position_order->position_side, PositionSide::Long);
}

TEST_F(InfraLogTestFixture, UnchangedPositionAndOrderDoNotEmitExtraSnapshotRows)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    WriteBinanceCsv(
        tmp_dir / "btc.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 100.0, 100.0, 100.0, 100.0, 1000.0, 90000u, 1000.0, 1, 0.0, 0.0 },
            { 120000u, 100.0, 100.0, 100.0, 100.0, 1000.0, 150000u, 1000.0, 1, 0.0, 0.0 }
        });

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "btc.csv").string() } },
            logger,
            1000.0,
            0,
            3300u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto position_rows = FilterRowsByModule(QTrading::Log::LogModule::Position);
    const auto order_rows = FilterRowsByModule(QTrading::Log::LogModule::Order);
    ASSERT_EQ(position_rows.size(), 1u);
    EXPECT_TRUE(order_rows.empty());
}

TEST_F(InfraLogTestFixture, PositionChangeWithoutOrderChangeEmitsOnlyPositionSnapshot)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    WriteBinanceCsv(
        tmp_dir / "btc.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 110.0, 110.0, 110.0, 110.0, 1000.0, 90000u, 1000.0, 1, 0.0, 0.0 }
        });

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "btc.csv").string() } },
            logger,
            1000.0,
            0,
            3400u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 0.5, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto position_rows = FilterRowsByModule(QTrading::Log::LogModule::Position);
    const auto order_rows = FilterRowsByModule(QTrading::Log::LogModule::Order);
    ASSERT_EQ(position_rows.size(), 2u);
    EXPECT_TRUE(order_rows.empty());

    const auto* first_position = RowPayloadCast<QTrading::dto::Position>(position_rows[0].row);
    const auto* second_position = RowPayloadCast<QTrading::dto::Position>(position_rows[1].row);
    ASSERT_NE(first_position, nullptr);
    ASSERT_NE(second_position, nullptr);
    EXPECT_LT(first_position->quantity, second_position->quantity);
    EXPECT_GT(second_position->entry_price, first_position->entry_price);
}

TEST_F(InfraLogTestFixture, OrderChangeWithoutPositionChangeEmitsOnlyOrderSnapshot)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    WriteBinanceCsv(
        tmp_dir / "btc.csv",
        { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "btc.csv").string() } },
            logger,
            1000.0,
            0,
            3500u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, 90.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto position_rows = FilterRowsByModule(QTrading::Log::LogModule::Position);
    const auto order_rows = FilterRowsByModule(QTrading::Log::LogModule::Order);
    EXPECT_TRUE(position_rows.empty());
    ASSERT_EQ(order_rows.size(), 1u);

    const auto* order = RowPayloadCast<QTrading::dto::Order>(order_rows.front().row);
    ASSERT_NE(order, nullptr);
    EXPECT_EQ(order->instrument_type, QTrading::Dto::Trading::InstrumentType::Perp);
    EXPECT_EQ(order->symbol, "BTCUSDT");
    EXPECT_DOUBLE_EQ(order->quantity, 1.0);
    EXPECT_DOUBLE_EQ(order->price, 90.0);
}

TEST_F(InfraLogTestFixture, RowPayloadCastReturnsNullptrForInvalidOrEmptyRow)
{
    const QTrading::Log::Row* null_row = nullptr;
    EXPECT_EQ(RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::RunMetadataDto>(null_row), nullptr);

    QTrading::Log::Row invalid_row{
        QTrading::Log::Logger::kInvalidModuleId,
        0u,
        {}
    };
    EXPECT_EQ(RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::RunMetadataDto>(&invalid_row), nullptr);

    QTrading::Log::Row empty_payload_row{
        ModuleId(QTrading::Log::LogModule::RunMetadata),
        0u,
        {}
    };
    EXPECT_EQ(RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::RunMetadataDto>(&empty_payload_row), nullptr);
}

TEST_F(InfraLogTestFixture, DrainAndSortRowsByArrivalDistinguishesArrivalFromBusinessSort)
{
    auto log_run_metadata = [&](uint64_t ts, uint64_t run_id, const char* strategy_name) {
        QTrading::Utils::GlobalTimestamp.store(ts);

        QTrading::Log::FileLogger::FeatherV2::RunMetadataDto payload{};
        payload.run_id = run_id;
        payload.strategy_name = strategy_name;
        payload.strategy_version = "1";
        payload.strategy_params = "{}";
        payload.dataset = "arrival-order";

        ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::RunMetadata), payload));
    };

    log_run_metadata(30u, 1u, "first-arrival");
    log_run_metadata(10u, 2u, "second-arrival");
    log_run_metadata(20u, 3u, "third-arrival");

    StopLogger();

    auto arrival_rows = DrainAndSortRowsByArrival();
    ASSERT_EQ(arrival_rows.size(), 3u);

    EXPECT_EQ(arrival_rows[0].arrival_index, 0u);
    EXPECT_EQ(arrival_rows[1].arrival_index, 1u);
    EXPECT_EQ(arrival_rows[2].arrival_index, 2u);
    EXPECT_EQ(arrival_rows[0].row->ts, 30u);
    EXPECT_EQ(arrival_rows[1].row->ts, 10u);
    EXPECT_EQ(arrival_rows[2].row->ts, 20u);

    const auto* first_arrival = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::RunMetadataDto>(arrival_rows[0].row);
    const auto* second_arrival = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::RunMetadataDto>(arrival_rows[1].row);
    const auto* third_arrival = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::RunMetadataDto>(arrival_rows[2].row);
    ASSERT_NE(first_arrival, nullptr);
    ASSERT_NE(second_arrival, nullptr);
    ASSERT_NE(third_arrival, nullptr);
    EXPECT_EQ(first_arrival->strategy_name, "first-arrival");
    EXPECT_EQ(second_arrival->strategy_name, "second-arrival");
    EXPECT_EQ(third_arrival->strategy_name, "third-arrival");

    auto business_sorted = arrival_rows;
    std::sort(
        business_sorted.begin(),
        business_sorted.end(),
        [](const ArrivedRowView& lhs, const ArrivedRowView& rhs) {
            if (lhs.row->ts != rhs.row->ts) {
                return lhs.row->ts < rhs.row->ts;
            }
            return lhs.row->module_id < rhs.row->module_id;
        });

    EXPECT_EQ(business_sorted[0].row->ts, 10u);
    EXPECT_EQ(business_sorted[1].row->ts, 20u);
    EXPECT_EQ(business_sorted[2].row->ts, 30u);
    EXPECT_NE(arrival_rows[0].row->ts, business_sorted[0].row->ts);

    const auto* first_business = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::RunMetadataDto>(business_sorted[0].row);
    const auto* second_business = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::RunMetadataDto>(business_sorted[1].row);
    const auto* third_business = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::RunMetadataDto>(business_sorted[2].row);
    ASSERT_NE(first_business, nullptr);
    ASSERT_NE(second_business, nullptr);
    ASSERT_NE(third_business, nullptr);
    EXPECT_EQ(first_business->strategy_name, "second-arrival");
    EXPECT_EQ(second_business->strategy_name, "third-arrival");
    EXPECT_EQ(third_business->strategy_name, "first-arrival");
}

TEST_F(InfraLogTestFixture, FilterRowsByModuleKeepsOnlyRequestedModuleInArrivalOrder)
{
    auto log_run_metadata = [&](uint64_t ts, uint64_t run_id, const char* strategy_name) {
        QTrading::Utils::GlobalTimestamp.store(ts);

        QTrading::Log::FileLogger::FeatherV2::RunMetadataDto payload{};
        payload.run_id = run_id;
        payload.strategy_name = strategy_name;
        payload.strategy_version = "1";
        payload.strategy_params = "{}";
        payload.dataset = "filter-by-module";

        ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::RunMetadata), payload));
    };

    auto log_account = [&](uint64_t ts, double balance) {
        QTrading::Utils::GlobalTimestamp.store(ts);

        QTrading::dto::AccountLog payload{};
        payload.balance = balance;
        payload.unreal_pnl = balance / 10.0;
        payload.equity = balance;
        payload.perp_wallet_balance = balance;
        payload.perp_available_balance = balance;
        payload.perp_ledger_value = balance;
        payload.spot_cash_balance = 0.0;
        payload.spot_available_balance = 0.0;
        payload.spot_inventory_value = 0.0;
        payload.spot_ledger_value = 0.0;
        payload.total_cash_balance = balance;
        payload.total_ledger_value = balance;

        ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::Account), payload));
    };

    log_run_metadata(1u, 1u, "before-account");
    log_account(2u, 100.0);
    log_account(3u, 200.0);
    log_run_metadata(4u, 2u, "after-account");

    StopLogger();

    const auto account_rows = FilterRowsByModule(QTrading::Log::LogModule::Account);
    ASSERT_EQ(account_rows.size(), 2u);

    EXPECT_EQ(account_rows[0].arrival_index, 1u);
    EXPECT_EQ(account_rows[1].arrival_index, 2u);
    EXPECT_EQ(account_rows[0].row->module_id, ModuleId(QTrading::Log::LogModule::Account));
    EXPECT_EQ(account_rows[1].row->module_id, ModuleId(QTrading::Log::LogModule::Account));
    EXPECT_EQ(account_rows[0].row->ts, 2u);
    EXPECT_EQ(account_rows[1].row->ts, 3u);

    const auto* first_account = RowPayloadCast<QTrading::dto::AccountLog>(account_rows[0].row);
    const auto* second_account = RowPayloadCast<QTrading::dto::AccountLog>(account_rows[1].row);
    ASSERT_NE(first_account, nullptr);
    ASSERT_NE(second_account, nullptr);
    EXPECT_DOUBLE_EQ(first_account->balance, 100.0);
    EXPECT_DOUBLE_EQ(second_account->balance, 200.0);
}

TEST_F(InfraLogTestFixture, AssertSingleStepEnvelopeValidatesMarketEventRowsFromSingleStep)
{
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::MarketEventDto;

    WriteBinanceCsv(
        tmp_dir / "btc.csv",
        { { 0u, 100.0, 101.0, 99.0, 100.5, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });
    WriteBinanceCsv(
        tmp_dir / "eth.csv",
        { { 0u, 200.0, 201.0, 199.0, 200.5, 2000.0, 30000u, 2000.0, 1, 0.0, 0.0 } });

    constexpr uint64_t expected_run_id = 4242u;

    {
        BinanceExchange exchange(
            {
                { "BTCUSDT", (tmp_dir / "btc.csv").string() },
                { "ETHUSDT", (tmp_dir / "eth.csv").string() }
            },
            logger,
            1000.0,
            0,
            expected_run_id);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        const auto dto = market_channel->Receive();
        ASSERT_TRUE(dto.has_value());
        EXPECT_EQ(dto->get()->Timestamp, 0u);
    }

    StopLogger();

    const auto market_event_rows = FilterRowsByModule(QTrading::Log::LogModule::MarketEvent);
    ASSERT_EQ(market_event_rows.size(), 2u);

    AssertSingleStepEnvelope<MarketEventDto>(
        market_event_rows,
        expected_run_id,
        1u,
        0u,
        0u);

    const auto* first_event = RowPayloadCast<MarketEventDto>(market_event_rows[0].row);
    const auto* second_event = RowPayloadCast<MarketEventDto>(market_event_rows[1].row);
    ASSERT_NE(first_event, nullptr);
    ASSERT_NE(second_event, nullptr);
    EXPECT_EQ(first_event->symbol, "BTCUSDT");
    EXPECT_EQ(second_event->symbol, "ETHUSDT");
}

TEST_F(InfraLogTestFixture, LegacyLogContractSnapshotBuildsDeterministicEqualityComparableRows)
{
    QTrading::Utils::GlobalTimestamp.store(11u);
    QTrading::Log::FileLogger::FeatherV2::RunMetadataDto run_metadata{};
    run_metadata.run_id = 9001u;
    run_metadata.strategy_name = "snapshot-test";
    run_metadata.strategy_version = "1";
    run_metadata.strategy_params = "{}";
    run_metadata.dataset = "fixture";
    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::RunMetadata), run_metadata));

    QTrading::Utils::GlobalTimestamp.store(22u);
    QTrading::Log::FileLogger::FeatherV2::MarketEventDto market_event{};
    market_event.run_id = 9001u;
    market_event.step_seq = 3u;
    market_event.event_seq = 1u;
    market_event.ts_local = 999u;
    market_event.symbol = "BTCUSDT";
    market_event.has_kline = true;
    market_event.close = 123.45;
    ASSERT_TRUE(logger->Log(QTrading::Log::LogModuleToString(QTrading::Log::LogModule::MarketEvent), market_event));

    StopLogger();

    std::vector<LegacyLogContractSnapshot> actual;
    for (const auto& row : rows()) {
        const auto snapshot = BuildLegacyLogContractSnapshot(&row);
        ASSERT_TRUE(snapshot.has_value());
        actual.push_back(*snapshot);
    }

    const std::vector<LegacyLogContractSnapshot> expected{
        LegacyLogContractSnapshot{
            QTrading::Log::LogModuleToString(QTrading::Log::LogModule::RunMetadata),
            ModuleId(QTrading::Log::LogModule::RunMetadata),
            11u,
            9001u,
            0u,
            0u,
            ""
        },
        LegacyLogContractSnapshot{
            QTrading::Log::LogModuleToString(QTrading::Log::LogModule::MarketEvent),
            ModuleId(QTrading::Log::LogModule::MarketEvent),
            22u,
            9001u,
            3u,
            1u,
            "BTCUSDT"
        }
    };

    EXPECT_EQ(actual, expected);
}

TEST_F(DeterministicReplayFixture, InfraLogSameReplayTwiceProducesIdenticalLegacyLogContractSnapshots)
{
    const auto first = RunReplayOnce("first");
    const auto second = RunReplayOnce("second");

    ASSERT_FALSE(first.empty());
    EXPECT_EQ(first, second);
}

TEST_F(DeterministicReplayFixture, SingleSymbolGoldenReplayProducesExpectedLegacyLogContractSnapshots)
{
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    const auto actual = RunGoldenScenario(
        "golden-single-symbol",
        [&](const fs::path& replay_dir, const std::shared_ptr<QTrading::Log::SinkLogger>& replay_logger) {
            WriteBinanceCsv(
                replay_dir / "btc.csv",
                {
                    { 0u, 100.0, 101.0, 99.0, 100.5, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
                    { 60000u, 101.0, 102.0, 100.0, 101.5, 1100.0, 90000u, 1100.0, 1, 0.0, 0.0 }
                });

            BinanceExchange exchange(
                { { "BTCUSDT", (replay_dir / "btc.csv").string() } },
                replay_logger,
                MakeDeterministicAccount(),
                kRunId);
            auto market_channel = exchange.get_market_channel();
            while (exchange.step()) {
                ASSERT_TRUE(market_channel->Receive().has_value());
            }
        },
        { QTrading::Log::LogModule::RunMetadata, QTrading::Log::LogModule::MarketEvent });

    ASSERT_EQ(actual.size(), 3u);
    EXPECT_EQ(actual.front(), ExpectedSnapshot(QTrading::Log::LogModule::RunMetadata, 0u, kRunId));

    const auto& first_step = actual[1];
    const auto& second_step = actual[2];

    EXPECT_EQ(first_step.module_name, QTrading::Log::LogModuleToString(QTrading::Log::LogModule::MarketEvent));
    EXPECT_EQ(second_step.module_name, QTrading::Log::LogModuleToString(QTrading::Log::LogModule::MarketEvent));
    EXPECT_EQ(first_step.module_id, 7u);
    EXPECT_EQ(second_step.module_id, 7u);
    EXPECT_EQ(first_step.run_id, kRunId);
    EXPECT_EQ(second_step.run_id, kRunId);
    EXPECT_EQ(first_step.step_seq, 1u);
    EXPECT_EQ(second_step.step_seq, 2u);
    EXPECT_EQ(first_step.event_seq, 0u);
    EXPECT_EQ(second_step.event_seq, 0u);
    EXPECT_EQ(first_step.symbol, "BTCUSDT");
    EXPECT_EQ(second_step.symbol, "BTCUSDT");
    EXPECT_TRUE(first_step.ts == 0u || first_step.ts == 60000u || first_step.ts == 120000u);
    EXPECT_TRUE(second_step.ts == 0u || second_step.ts == 60000u || second_step.ts == 120000u);
    EXPECT_GE(second_step.ts, first_step.ts);
}

TEST_F(DeterministicReplayFixture, DualSymbolHolesGoldenReplayProducesExpectedLegacyLogContractSnapshots)
{
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    const auto actual = RunGoldenScenario(
        "golden-dual-symbol-holes",
        [&](const fs::path& replay_dir, const std::shared_ptr<QTrading::Log::SinkLogger>& replay_logger) {
            WriteBinanceCsv(
                replay_dir / "btc.csv",
                {
                    { 0u, 100.0, 101.0, 99.0, 100.5, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
                    { 60000u, 100.0, 101.0, 99.0, 100.5, 1000.0, 90000u, 1000.0, 1, 0.0, 0.0 },
                    { 120000u, 102.0, 103.0, 101.0, 102.5, 1200.0, 150000u, 1200.0, 1, 0.0, 0.0 }
                });
            WriteBinanceCsv(
                replay_dir / "eth.csv",
                {
                    { 0u, 200.0, 201.0, 199.0, 200.5, 2000.0, 30000u, 2000.0, 1, 0.0, 0.0 },
                    { 60000u, 201.0, 202.0, 200.0, 201.5, 2100.0, 90000u, 2100.0, 1, 0.0, 0.0 },
                    { 120000u, 202.0, 203.0, 201.0, 202.5, 2200.0, 150000u, 2200.0, 1, 0.0, 0.0 }
                });

            BinanceExchange exchange(
                {
                    { "BTCUSDT", (replay_dir / "btc.csv").string() },
                    { "ETHUSDT", (replay_dir / "eth.csv").string() }
                },
                replay_logger,
                MakeDeterministicAccount(),
                kRunId);
            auto market_channel = exchange.get_market_channel();
            while (exchange.step()) {
                ASSERT_TRUE(market_channel->Receive().has_value());
            }
        },
        { QTrading::Log::LogModule::RunMetadata, QTrading::Log::LogModule::MarketEvent });

    ASSERT_EQ(actual.size(), 7u);
    EXPECT_EQ(actual.front(), ExpectedSnapshot(QTrading::Log::LogModule::RunMetadata, 0u, kRunId));

    uint64_t previous_ts = 0u;
    for (size_t step_index = 0; step_index < 3; ++step_index) {
        const auto& first = actual[1u + step_index * 2u];
        const auto& second = actual[2u + step_index * 2u];

        EXPECT_EQ(first.module_name, QTrading::Log::LogModuleToString(QTrading::Log::LogModule::MarketEvent));
        EXPECT_EQ(second.module_name, QTrading::Log::LogModuleToString(QTrading::Log::LogModule::MarketEvent));
        EXPECT_EQ(first.module_id, 7u);
        EXPECT_EQ(second.module_id, 7u);
        EXPECT_EQ(first.run_id, kRunId);
        EXPECT_EQ(second.run_id, kRunId);
        EXPECT_EQ(first.step_seq, step_index + 1u);
        EXPECT_EQ(second.step_seq, step_index + 1u);
        EXPECT_EQ(first.event_seq, 0u);
        EXPECT_EQ(second.event_seq, 1u);
        EXPECT_EQ(first.symbol, "BTCUSDT");
        EXPECT_EQ(second.symbol, "ETHUSDT");

        EXPECT_EQ(first.ts, second.ts);
        EXPECT_TRUE(first.ts == 0u || first.ts == 60000u || first.ts == 120000u);
        EXPECT_GE(first.ts, previous_ts);
        previous_ts = first.ts;
    }

    EXPECT_EQ(actual.back().ts, 120000u);
}

TEST_F(DeterministicReplayFixture, FundingInterpolationGoldenReplayProducesExpectedLegacyLogContractSnapshots)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    const auto actual = RunGoldenScenario(
        "golden-funding-interpolation",
        [&](const fs::path& replay_dir, const std::shared_ptr<QTrading::Log::SinkLogger>& replay_logger) {
            WriteBinanceCsv(
                replay_dir / "btc_trade.csv",
                {
                    { 0u, 100.0, 101.0, 99.0, 100.5, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
                    { 60000u, 101.0, 102.0, 100.0, 101.5, 1100.0, 90000u, 1100.0, 1, 0.0, 0.0 }
                });
            WriteBinanceCsv(
                replay_dir / "btc_mark.csv",
                { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });
            WriteBinanceCsv(
                replay_dir / "btc_index.csv",
                { { 0u, 99.0, 99.0, 99.0, 99.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });
            WriteFundingCsv(
                replay_dir / "btc_funding.csv",
                { { 60000u, 0.001, std::nullopt } });

            auto account = MakeDeterministicAccount();
            BinanceExchange exchange(
                { BinanceExchange::SymbolDataset{
                    "BTCUSDT",
                    (replay_dir / "btc_trade.csv").string(),
                    std::optional<std::string>((replay_dir / "btc_funding.csv").string()),
                    (replay_dir / "btc_mark.csv").string(),
                    (replay_dir / "btc_index.csv").string() } },
                replay_logger,
                account,
                kRunId);
            auto market_channel = exchange.get_market_channel();

            ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
            ASSERT_TRUE(exchange.step());
            ASSERT_TRUE(market_channel->Receive().has_value());
            ASSERT_TRUE(exchange.step());
            ASSERT_TRUE(market_channel->Receive().has_value());
        },
        { QTrading::Log::LogModule::RunMetadata, QTrading::Log::LogModule::MarketEvent, QTrading::Log::LogModule::FundingEvent });

    const std::vector<LegacyLogContractSnapshot> expected{
        ExpectedSnapshot(QTrading::Log::LogModule::RunMetadata, 0u, kRunId),
        ExpectedSnapshot(QTrading::Log::LogModule::MarketEvent, 60000u, kRunId, 1u, 0u, "BTCUSDT"),
        ExpectedSnapshot(QTrading::Log::LogModule::MarketEvent, 60000u, kRunId, 2u, 0u, "BTCUSDT"),
        ExpectedSnapshot(QTrading::Log::LogModule::FundingEvent, 60000u, kRunId, 2u, 1u, "BTCUSDT")
    };

    EXPECT_EQ(actual, expected);
}

TEST_F(DeterministicReplayFixture, SpotPerpMixedBookGoldenReplayProducesExpectedLegacyLogContractSnapshots)
{
    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    const auto actual = RunGoldenScenario(
        "golden-spot-perp-mixed",
        [&](const fs::path& replay_dir, const std::shared_ptr<QTrading::Log::SinkLogger>& replay_logger) {
            WriteBinanceCsv(
                replay_dir / "btc_spot.csv",
                { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });
            WriteBinanceCsv(
                replay_dir / "eth_perp.csv",
                { { 0u, 200.0, 200.0, 200.0, 200.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });

            auto account = MakeDeterministicAccount(1000.0, 1000.0);
            BinanceExchange exchange(
                {
                    BinanceExchange::SymbolDataset{
                        "BTCUSDT_SPOT",
                        (replay_dir / "btc_spot.csv").string(),
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        InstrumentType::Spot },
                    BinanceExchange::SymbolDataset{
                        "ETHUSDT_PERP",
                        (replay_dir / "eth_perp.csv").string(),
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        InstrumentType::Perp }
                },
                replay_logger,
                account,
                kRunId);
            auto market_channel = exchange.get_market_channel();

            ASSERT_TRUE(exchange.spot.place_order("BTCUSDT_SPOT", 1.0, 100.0, OrderSide::Buy));
            ASSERT_TRUE(exchange.perp.place_order("ETHUSDT_PERP", 1.0, OrderSide::Sell));
            ASSERT_TRUE(exchange.step());
            ASSERT_TRUE(market_channel->Receive().has_value());
        },
        { QTrading::Log::LogModule::RunMetadata, QTrading::Log::LogModule::Account, QTrading::Log::LogModule::MarketEvent });

    const std::vector<LegacyLogContractSnapshot> expected{
        ExpectedSnapshot(QTrading::Log::LogModule::RunMetadata, 0u, kRunId),
        ExpectedSnapshot(QTrading::Log::LogModule::Account, 0u),
        ExpectedSnapshot(QTrading::Log::LogModule::MarketEvent, 0u, kRunId, 1u, 0u, "BTCUSDT_SPOT"),
        ExpectedSnapshot(QTrading::Log::LogModule::MarketEvent, 0u, kRunId, 1u, 1u, "ETHUSDT_PERP")
    };

    EXPECT_EQ(actual, expected);
}

TEST_F(DeterministicReplayFixture, HedgeModeGoldenReplayProducesExpectedLegacyLogContractSnapshots)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    const auto actual = RunGoldenScenario(
        "golden-hedge-mode",
        [&](const fs::path& replay_dir, const std::shared_ptr<QTrading::Log::SinkLogger>& replay_logger) {
            WriteBinanceCsv(
                replay_dir / "btc.csv",
                { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });

            auto account = MakeDeterministicAccount();
            Account::AccountInitConfig cfg{};
            account->set_strict_binance_mode(false);
            BinanceExchange exchange(
                { { "BTCUSDT", (replay_dir / "btc.csv").string() } },
                replay_logger,
                account,
                kRunId);
            auto market_channel = exchange.get_market_channel();

            ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy, PositionSide::Long));
            ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Sell, PositionSide::Short));
            ASSERT_TRUE(exchange.step());
            ASSERT_TRUE(market_channel->Receive().has_value());
            EXPECT_EQ(exchange.get_all_positions().size(), 2u);
        },
        { QTrading::Log::LogModule::RunMetadata, QTrading::Log::LogModule::Position, QTrading::Log::LogModule::MarketEvent });

    const std::vector<LegacyLogContractSnapshot> expected{
        ExpectedSnapshot(QTrading::Log::LogModule::RunMetadata, 0u, kRunId),
        ExpectedSnapshot(QTrading::Log::LogModule::Position, 0u, 0u, 0u, 0u, "BTCUSDT"),
        ExpectedSnapshot(QTrading::Log::LogModule::Position, 0u, 0u, 0u, 0u, "BTCUSDT"),
        ExpectedSnapshot(QTrading::Log::LogModule::MarketEvent, 0u, kRunId, 1u, 0u, "BTCUSDT")
    };

    EXPECT_EQ(actual, expected);
}

TEST_F(DeterministicReplayFixture, LiquidationGoldenReplayProducesExpectedLegacyLogContractSnapshots)
{
    using QTrading::Dto::Market::Binance::TradeKlineDto;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    const auto actual = RunGoldenScenario(
        "golden-liquidation",
        [&](const fs::path& replay_dir, const std::shared_ptr<QTrading::Log::SinkLogger>& replay_logger) {
            WriteBinanceCsv(
                replay_dir / "liq_trade.csv",
                { { 0u, 1.0, 1.0, 1.0, 1.0, 10000.0, 30000u, 10000.0, 1, 0.0, 0.0 } });
            WriteBinanceCsv(
                replay_dir / "liq_mark.csv",
                { { 0u, 1.0, 1.0, 1.0, 1.0, 10000.0, 30000u, 10000.0, 1, 0.0, 0.0 } });

            auto account = std::make_shared<Account>(350000.0, 0);
            account->set_symbol_leverage("BTCUSDT", 75.0);
            ASSERT_TRUE(account->place_order("BTCUSDT", 5000.0, 500.0, OrderSide::Buy, PositionSide::Both));
            TradeKlineDto open_kline{};
            open_kline.OpenPrice = 500.0;
            open_kline.HighPrice = 500.0;
            open_kline.LowPrice = 500.0;
            open_kline.ClosePrice = 500.0;
            open_kline.Volume = 10000.0;
            account->update_positions(
                std::unordered_map<std::string, TradeKlineDto>{ { "BTCUSDT", open_kline } },
                std::unordered_map<std::string, double>{ { "BTCUSDT", 500.0 } });

            BinanceExchange exchange(
                { BinanceExchange::SymbolDataset{
                    "BTCUSDT",
                    (replay_dir / "liq_trade.csv").string(),
                    std::nullopt,
                    (replay_dir / "liq_mark.csv").string() } },
                replay_logger,
                account,
                kRunId);
            auto market_channel = exchange.get_market_channel();

            ASSERT_TRUE(exchange.step());
            ASSERT_TRUE(market_channel->Receive().has_value());
            EXPECT_TRUE(exchange.get_all_positions().empty());
        },
        {
            QTrading::Log::LogModule::RunMetadata,
            QTrading::Log::LogModule::Account,
            QTrading::Log::LogModule::MarketEvent,
            QTrading::Log::LogModule::AccountEvent,
            QTrading::Log::LogModule::PositionEvent
        });

    const std::vector<LegacyLogContractSnapshot> expected{
        ExpectedSnapshot(QTrading::Log::LogModule::RunMetadata, 0u, kRunId),
        ExpectedSnapshot(QTrading::Log::LogModule::Account, 0u),
        ExpectedSnapshot(QTrading::Log::LogModule::MarketEvent, 0u, kRunId, 1u, 0u, "BTCUSDT"),
        ExpectedSnapshot(QTrading::Log::LogModule::AccountEvent, 0u, kRunId, 1u, 1u, "BTCUSDT"),
        ExpectedSnapshot(QTrading::Log::LogModule::AccountEvent, 0u, kRunId, 1u, 2u, ""),
        ExpectedSnapshot(QTrading::Log::LogModule::PositionEvent, 0u, kRunId, 1u, 3u, "BTCUSDT")
    };

    EXPECT_EQ(actual, expected);
}

TEST_F(DeterministicReplayFixture, AsyncLatencyAndRejectionGoldenReplayProducesExpectedLegacyLogContractSnapshots)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    BinanceExchange::AsyncOrderAck rejected_ack{};
    const auto actual = RunGoldenScenario(
        "golden-async-rejection",
        [&](const fs::path& replay_dir, const std::shared_ptr<QTrading::Log::SinkLogger>& replay_logger) {
            WriteBinanceCsv(
                replay_dir / "btc.csv",
                {
                    { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
                    { 60000u, 100.0, 100.0, 100.0, 100.0, 1000.0, 90000u, 1000.0, 1, 0.0, 0.0 }
                });
            WriteBinanceCsv(
                replay_dir / "eth.csv",
                {
                    { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
                    { 60000u, 100.0, 100.0, 100.0, 100.0, 1000.0, 90000u, 1000.0, 1, 0.0, 0.0 }
                });

            BinanceExchange exchange(
                {
                    { "BTCUSDT", (replay_dir / "btc.csv").string() },
                    { "ETHUSDT", (replay_dir / "eth.csv").string() }
                },
                replay_logger,
                MakeDeterministicAccount(),
                kRunId);
            exchange.set_order_latency_bars(1);
            auto market_channel = exchange.get_market_channel();

            ASSERT_TRUE(exchange.step());
            ASSERT_TRUE(market_channel->Receive().has_value());
            ASSERT_TRUE(exchange.perp.place_order(
                "BTCUSDT", 1.0, 90.0, OrderSide::Buy, PositionSide::Both, false, "cid-open"));
            ASSERT_TRUE(exchange.spot.place_order("ETHUSDT", 1.0, 100.0, OrderSide::Sell));
            ASSERT_EQ(exchange.drain_async_order_acks().size(), 2u);

            ASSERT_TRUE(exchange.step());
            ASSERT_TRUE(market_channel->Receive().has_value());
            const auto resolved = exchange.drain_async_order_acks();
            ASSERT_EQ(resolved.size(), 2u);
            for (const auto& ack : resolved) {
                if (ack.symbol == "ETHUSDT") {
                    rejected_ack = ack;
                }
            }
        },
        {
            QTrading::Log::LogModule::RunMetadata,
            QTrading::Log::LogModule::Order,
            QTrading::Log::LogModule::MarketEvent,
            QTrading::Log::LogModule::AccountEvent,
            QTrading::Log::LogModule::OrderEvent
        });

    EXPECT_EQ(rejected_ack.status, BinanceExchange::AsyncOrderAck::Status::Rejected);

    const std::vector<LegacyLogContractSnapshot> expected{
        ExpectedSnapshot(QTrading::Log::LogModule::RunMetadata, 0u, kRunId),
        ExpectedSnapshot(QTrading::Log::LogModule::MarketEvent, 0u, kRunId, 1u, 0u, "BTCUSDT"),
        ExpectedSnapshot(QTrading::Log::LogModule::MarketEvent, 0u, kRunId, 1u, 1u, "ETHUSDT"),
        ExpectedSnapshot(QTrading::Log::LogModule::AccountEvent, 0u, kRunId, 1u, 2u, ""),
        ExpectedSnapshot(QTrading::Log::LogModule::Order, 60000u, 0u, 0u, 0u, "BTCUSDT"),
        ExpectedSnapshot(QTrading::Log::LogModule::MarketEvent, 60000u, kRunId, 2u, 0u, "BTCUSDT"),
        ExpectedSnapshot(QTrading::Log::LogModule::MarketEvent, 60000u, kRunId, 2u, 1u, "ETHUSDT"),
        ExpectedSnapshot(QTrading::Log::LogModule::AccountEvent, 60000u, kRunId, 2u, 2u, ""),
        ExpectedSnapshot(QTrading::Log::LogModule::OrderEvent, 60000u, kRunId, 2u, 3u, "BTCUSDT")
    };

    EXPECT_EQ(actual, expected);
}

TEST_F(DeterministicReplayFixture, BasisStressOverlayBlockGoldenReplayProducesExpectedLegacyLogContractSnapshots)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    BinanceExchange::StatusSnapshot snap{};
    const auto actual = RunGoldenScenario(
        "golden-basis-stress",
        [&](const fs::path& replay_dir, const std::shared_ptr<QTrading::Log::SinkLogger>& replay_logger) {
            WriteBinanceCsv(
                replay_dir / "trade.csv",
                { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });
            WriteBinanceCsv(
                replay_dir / "mark.csv",
                { { 0u, 130.0, 130.0, 130.0, 130.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });
            WriteBinanceCsv(
                replay_dir / "index.csv",
                { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });

            BinanceExchange exchange(
                { BinanceExchange::SymbolDataset{
                    "BTCUSDT",
                    (replay_dir / "trade.csv").string(),
                    std::nullopt,
                    (replay_dir / "mark.csv").string(),
                    (replay_dir / "index.csv").string() } },
                replay_logger,
                MakeDeterministicAccount(),
                kRunId);
            exchange.set_mark_index_basis_thresholds_bps(500.0, 1500.0);
            exchange.set_basis_stress_blocks_opening_orders(true);
            auto market_channel = exchange.get_market_channel();

            ASSERT_TRUE(exchange.step());
            ASSERT_TRUE(market_channel->Receive().has_value());
            EXPECT_FALSE(exchange.perp.place_order("BTCUSDT", 1.0, 90.0, OrderSide::Buy));
            exchange.FillStatusSnapshot(snap);
        },
        { QTrading::Log::LogModule::RunMetadata, QTrading::Log::LogModule::MarketEvent });

    EXPECT_EQ(snap.basis_stress_symbols, 1u);
    EXPECT_EQ(snap.basis_stress_blocked_orders, 1u);

    const std::vector<LegacyLogContractSnapshot> expected{
        ExpectedSnapshot(QTrading::Log::LogModule::RunMetadata, 0u, kRunId),
        ExpectedSnapshot(QTrading::Log::LogModule::MarketEvent, 0u, kRunId, 1u, 0u, "BTCUSDT")
    };

    EXPECT_EQ(actual, expected);
}

TEST_F(InfraLogTestFixture, OrderEventAcceptedIsEmittedWhenNewOrderRemainsOpen)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventDto;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventType;

    WriteBinanceCsv(
        tmp_dir / "accepted.csv",
        { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "accepted.csv").string() } },
            logger,
            1000.0,
            0,
            3600u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, 90.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto order_event_rows = FilterRowsByModule(QTrading::Log::LogModule::OrderEvent);
    ASSERT_EQ(order_event_rows.size(), 1u);

    const auto* accepted = RowPayloadCast<OrderEventDto>(order_event_rows.front().row);
    ASSERT_NE(accepted, nullptr);
    EXPECT_EQ(accepted->event_type, static_cast<int32_t>(OrderEventType::Accepted));
    EXPECT_EQ(accepted->symbol, "BTCUSDT");
    EXPECT_DOUBLE_EQ(accepted->qty, 1.0);
    EXPECT_DOUBLE_EQ(accepted->price, 90.0);
    EXPECT_DOUBLE_EQ(accepted->exec_qty, 0.0);
    EXPECT_DOUBLE_EQ(accepted->remaining_qty, 1.0);
}

TEST_F(InfraLogTestFixture, OrderEventFilledCarriesExecQtyExecPriceAndRemainingQty)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventDto;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventType;

    WriteBinanceCsv(
        tmp_dir / "filled.csv",
        { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "filled.csv").string() } },
            logger,
            1000.0,
            0,
            3700u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.25, 100.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto order_event_rows = FilterRowsByModule(QTrading::Log::LogModule::OrderEvent);
    ASSERT_EQ(order_event_rows.size(), 1u);

    const auto* filled = RowPayloadCast<OrderEventDto>(order_event_rows.front().row);
    ASSERT_NE(filled, nullptr);
    EXPECT_EQ(filled->event_type, static_cast<int32_t>(OrderEventType::Filled));
    EXPECT_DOUBLE_EQ(filled->qty, 1.25);
    EXPECT_DOUBLE_EQ(filled->exec_qty, 1.25);
    EXPECT_DOUBLE_EQ(filled->exec_price, 100.0);
    EXPECT_DOUBLE_EQ(filled->remaining_qty, 0.0);
}

TEST_F(InfraLogTestFixture, OrderEventCanceledIsEmittedWhenOpenOrderIsCanceled)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventDto;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventType;

    WriteBinanceCsv(
        tmp_dir / "canceled.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 100.0, 100.0, 100.0, 100.0, 1000.0, 90000u, 1000.0, 1, 0.0, 0.0 }
        });

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "canceled.csv").string() } },
            logger,
            1000.0,
            0,
            3800u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, 90.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        exchange.perp.cancel_open_orders("BTCUSDT");
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto order_event_rows = FilterRowsByModule(QTrading::Log::LogModule::OrderEvent);
    ASSERT_EQ(order_event_rows.size(), 2u);

    const auto* canceled = RowPayloadCast<OrderEventDto>(order_event_rows[1].row);
    ASSERT_NE(canceled, nullptr);
    EXPECT_EQ(canceled->event_type, static_cast<int32_t>(OrderEventType::Canceled));
    EXPECT_EQ(canceled->symbol, "BTCUSDT");
    EXPECT_DOUBLE_EQ(canceled->exec_qty, 0.0);
    EXPECT_DOUBLE_EQ(canceled->remaining_qty, 1.0);
}

TEST_F(InfraLogTestFixture, OrderEventLifecycleKeepsAcceptedFilledCanceledForPartialThenCancel)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventDto;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventType;

    WriteBinanceCsv(
        tmp_dir / "partial_cancel.csv",
        {
            { 0u, 105.0, 105.0, 105.0, 105.0, 10.0, 30000u, 1050.0, 1, 0.0, 0.0 },
            { 60000u, 100.0, 100.0, 100.0, 100.0, 0.4, 90000u, 40.0, 1, 0.0, 0.0 },
            { 120000u, 100.0, 100.0, 100.0, 100.0, 10.0, 150000u, 1000.0, 1, 0.0, 0.0 }
        });

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "partial_cancel.csv").string() } },
            logger,
            1000.0,
            0,
            3900u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        exchange.perp.cancel_open_orders("BTCUSDT");
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto order_event_rows = FilterRowsByModule(QTrading::Log::LogModule::OrderEvent);
    ASSERT_GE(order_event_rows.size(), 3u);

    const auto* accepted = RowPayloadCast<OrderEventDto>(order_event_rows[0].row);
    const auto* filled = RowPayloadCast<OrderEventDto>(order_event_rows[1].row);
    const auto* canceled = RowPayloadCast<OrderEventDto>(order_event_rows[2].row);
    ASSERT_NE(accepted, nullptr);
    ASSERT_NE(filled, nullptr);
    ASSERT_NE(canceled, nullptr);

    EXPECT_EQ(accepted->event_type, static_cast<int32_t>(OrderEventType::Accepted));
    EXPECT_EQ(filled->event_type, static_cast<int32_t>(OrderEventType::Filled));
    EXPECT_EQ(canceled->event_type, static_cast<int32_t>(OrderEventType::Canceled));
    EXPECT_NEAR(filled->exec_qty, 0.4, 1e-12);
    EXPECT_NEAR(filled->remaining_qty, 0.6, 1e-12);
    EXPECT_NEAR(canceled->remaining_qty, 0.6, 1e-12);
}

TEST_F(InfraLogTestFixture, OrderEventFeeFieldsAreCorrectForSpotAndPerpFills)
{
    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventDto;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventType;

    WriteBinanceCsv(
        tmp_dir / "spot_fee.csv",
        { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });
    WriteBinanceCsv(
        tmp_dir / "perp_fee.csv",
        { { 0u, 200.0, 200.0, 200.0, 200.0, 1000.0, 30000u, 2000.0, 1, 0.0, 0.0 } });

    Account::AccountInitConfig cfg{};
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 1000.0;

    {
        BinanceExchange exchange(
            {
                BinanceExchange::SymbolDataset{
                    "BTCUSDT_SPOT",
                    (tmp_dir / "spot_fee.csv").string(),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    InstrumentType::Spot },
                BinanceExchange::SymbolDataset{
                    "ETHUSDT_PERP",
                    (tmp_dir / "perp_fee.csv").string(),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    InstrumentType::Perp }
            },
            logger,
            cfg,
            4000u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.spot.place_order("BTCUSDT_SPOT", 1.0, 100.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.perp.place_order("ETHUSDT_PERP", 2.0, 200.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto order_event_rows = FilterRowsByModule(QTrading::Log::LogModule::OrderEvent);
    ASSERT_EQ(order_event_rows.size(), 2u);

    const OrderEventDto* spot_event = nullptr;
    const OrderEventDto* perp_event = nullptr;
    for (const auto& row_view : order_event_rows) {
        const auto* payload = RowPayloadCast<OrderEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        ASSERT_EQ(payload->event_type, static_cast<int32_t>(OrderEventType::Filled));
        if (payload->symbol == "BTCUSDT_SPOT") {
            spot_event = payload;
        }
        if (payload->symbol == "ETHUSDT_PERP") {
            perp_event = payload;
        }
    }

    ASSERT_NE(spot_event, nullptr);
    ASSERT_NE(perp_event, nullptr);

    EXPECT_NEAR(spot_event->fee, 0.1, 1e-12);
    EXPECT_NEAR(spot_event->fee_rate, 0.001, 1e-12);
    EXPECT_EQ(spot_event->fee_asset, static_cast<int32_t>(Account::CommissionAsset::QuoteAsset));
    EXPECT_NEAR(spot_event->fee_native, 0.1, 1e-12);
    EXPECT_NEAR(spot_event->fee_quote_equiv, 0.1, 1e-12);

    EXPECT_NEAR(perp_event->fee, 0.2, 1e-12);
    EXPECT_NEAR(perp_event->fee_rate, 0.0005, 1e-12);
    EXPECT_EQ(perp_event->fee_asset, static_cast<int32_t>(Account::CommissionAsset::None));
    EXPECT_NEAR(perp_event->fee_native, 0.2, 1e-12);
    EXPECT_NEAR(perp_event->fee_quote_equiv, 0.2, 1e-12);
}

TEST_F(InfraLogTestFixture, OrderEventCapturesSpotBaseFeeCommissionAndCashflowFields)
{
    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventDto;

    WriteBinanceCsv(
        tmp_dir / "spot_base_fee.csv",
        { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });

    Account::AccountInitConfig cfg{};
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 0.0;
    auto account = std::make_shared<Account>(cfg);
    account->set_spot_commission_mode(Account::SpotCommissionMode::BaseOnBuyQuoteOnSell);

    {
        BinanceExchange exchange(
            {
                BinanceExchange::SymbolDataset{
                    "BTCUSDT_SPOT",
                    (tmp_dir / "spot_base_fee.csv").string(),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    InstrumentType::Spot }
            },
            logger,
            account,
            4100u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.spot.place_order("BTCUSDT_SPOT", 1.0, 100.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto order_event_rows = FilterRowsByModule(QTrading::Log::LogModule::OrderEvent);
    ASSERT_EQ(order_event_rows.size(), 1u);

    const auto* order_event = RowPayloadCast<OrderEventDto>(order_event_rows.front().row);
    ASSERT_NE(order_event, nullptr);
    EXPECT_EQ(
        order_event->commission_model_source,
        static_cast<int32_t>(Account::CommissionModelSource::ImputedBuyBase));
    EXPECT_EQ(order_event->fee_asset, static_cast<int32_t>(Account::CommissionAsset::BaseAsset));
    EXPECT_NEAR(order_event->spot_cash_delta, -100.0, 1e-12);
    EXPECT_NEAR(order_event->spot_inventory_delta, 0.999, 1e-12);
}

TEST_F(InfraLogTestFixture, OrderEventReflectsModeledExecutionResultsWhenExecutionModelsEnabled)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventDto;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventType;

    WriteBinanceCsv(
        tmp_dir / "impact.csv",
        { { 0u, 100.0, 120.0, 80.0, 100.0, 100.0, 30000u, 10000.0, 1, 0.0, 0.0 } });
    WriteBinanceCsv(
        tmp_dir / "taker.csv",
        { { 0u, 100.0, 110.0, 90.0, 100.0, 100.0, 30000u, 10000.0, 1, 90.0, 9000.0 } });
    WriteBinanceCsv(
        tmp_dir / "fill_prob.csv",
        { { 0u, 100.0, 110.0, 90.0, 100.0, 100.0, 30000u, 10000.0, 1, 0.0, 0.0 } });

    {
        Account::AccountInitConfig cfg{};
        cfg.perp_initial_wallet = 100000.0;
        auto account = std::make_shared<Account>(cfg);
        account->set_kline_volume_split_mode(Account::KlineVolumeSplitMode::LegacyTotalOnly);
        account->set_market_impact_slippage_enabled(true);
        account->set_market_impact_slippage_params(0.0, 500.0, 1.0, 0.0, 0.0);

        BinanceExchange exchange(
            { { "IMPACTUSDT", (tmp_dir / "impact.csv").string() } },
            logger,
            account,
            4200u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("IMPACTUSDT", 8.0, 110.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    {
        Account::AccountInitConfig cfg{};
        cfg.perp_initial_wallet = 100000.0;
        auto account = std::make_shared<Account>(cfg);
        account->set_kline_volume_split_mode(Account::KlineVolumeSplitMode::LegacyTotalOnly);
        account->set_taker_probability_model_enabled(true);
        account->set_taker_probability_model_coefficients(-1.0, 2.0, 0.5, 0.5, 0.0);

        BinanceExchange exchange(
            { { "TAKERUSDT", (tmp_dir / "taker.csv").string() } },
            logger,
            account,
            4201u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("TAKERUSDT", 1.0, 99.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    {
        Account::AccountInitConfig cfg{};
        cfg.perp_initial_wallet = 100000.0;
        auto account = std::make_shared<Account>(cfg);
        account->set_kline_volume_split_mode(Account::KlineVolumeSplitMode::LegacyTotalOnly);
        account->set_limit_fill_probability_enabled(true);
        account->set_limit_fill_probability_coefficients(1.0, 2.0, 2.0, 0.0, 0.0);

        BinanceExchange exchange(
            { { "FILLUSDT", (tmp_dir / "fill_prob.csv").string() } },
            logger,
            account,
            4202u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("FILLUSDT", 80.0, 99.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto order_event_rows = FilterRowsByModule(QTrading::Log::LogModule::OrderEvent);
    ASSERT_GE(order_event_rows.size(), 3u);

    const OrderEventDto* impact_event = nullptr;
    const OrderEventDto* taker_event = nullptr;
    const OrderEventDto* fill_probability_event = nullptr;
    for (const auto& row_view : order_event_rows) {
        const auto* payload = RowPayloadCast<OrderEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->event_type != static_cast<int32_t>(OrderEventType::Filled)) {
            continue;
        }
        if (payload->symbol == "IMPACTUSDT") {
            impact_event = payload;
        }
        if (payload->symbol == "TAKERUSDT") {
            taker_event = payload;
        }
        if (payload->symbol == "FILLUSDT") {
            fill_probability_event = payload;
        }
    }

    ASSERT_NE(impact_event, nullptr);
    ASSERT_NE(taker_event, nullptr);
    ASSERT_NE(fill_probability_event, nullptr);

    EXPECT_GT(impact_event->exec_price, 100.0);
    EXPECT_TRUE(taker_event->is_taker);
    EXPECT_NEAR(taker_event->fee_rate, 0.0005, 1e-12);
    EXPECT_LT(fill_probability_event->exec_qty, fill_probability_event->qty);
    EXPECT_GT(fill_probability_event->remaining_qty, 0.0);
}

TEST_F(InfraLogTestFixture, OrderEventPreservesQuoteOrderQtyForQuoteBasedMarketBuy)
{
    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventDto;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventType;

    WriteBinanceCsv(
        tmp_dir / "quote_buy.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 100.0, 100.0, 100.0, 100.0, 1000.0, 90000u, 1000.0, 1, 0.0, 0.0 }
        });

    Account::AccountInitConfig cfg{};
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 0.0;

    {
        BinanceExchange exchange(
            {
                BinanceExchange::SymbolDataset{
                    "BTCUSDT_SPOT",
                    (tmp_dir / "quote_buy.csv").string(),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    InstrumentType::Spot }
            },
            logger,
            cfg,
            4300u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        ASSERT_TRUE(exchange.spot.place_market_order_quote("BTCUSDT_SPOT", 100.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto order_event_rows = FilterRowsByModule(QTrading::Log::LogModule::OrderEvent);
    ASSERT_EQ(order_event_rows.size(), 1u);

    const auto* order_event = RowPayloadCast<OrderEventDto>(order_event_rows.front().row);
    ASSERT_NE(order_event, nullptr);
    EXPECT_EQ(order_event->event_type, static_cast<int32_t>(OrderEventType::Filled));
    EXPECT_EQ(order_event->symbol, "BTCUSDT_SPOT");
    EXPECT_DOUBLE_EQ(order_event->quote_order_qty, 100.0);
    EXPECT_GT(order_event->exec_qty, 0.0);
}

TEST_F(InfraLogTestFixture, RejectedAsyncOrderDoesNotEmitAcceptedOrFilledOrderEvents)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventDto;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventType;

    WriteBinanceCsv(
        tmp_dir / "async_rejected.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 100.0, 30000u, 100.0, 1, 0.0, 0.0 },
            { 60000u, 100.0, 100.0, 100.0, 100.0, 100.0, 90000u, 100.0, 1, 0.0, 0.0 }
        });

    BinanceExchange::AsyncOrderAck pending_ack{};
    BinanceExchange::AsyncOrderAck rejected_ack{};

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "async_rejected.csv").string() } },
            logger,
            1000.0,
            0,
            4400u);
        exchange.set_order_latency_bars(1);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        ASSERT_TRUE(exchange.spot.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Sell));
        auto pending = exchange.drain_async_order_acks();
        ASSERT_EQ(pending.size(), 1u);
        pending_ack = pending.front();

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        auto rejected = exchange.drain_async_order_acks();
        ASSERT_EQ(rejected.size(), 1u);
        rejected_ack = rejected.front();
    }

    StopLogger();

    EXPECT_EQ(pending_ack.status, BinanceExchange::AsyncOrderAck::Status::Pending);
    EXPECT_EQ(rejected_ack.status, BinanceExchange::AsyncOrderAck::Status::Rejected);

    const auto order_event_rows = FilterRowsByModule(QTrading::Log::LogModule::OrderEvent);
    for (const auto& row_view : order_event_rows) {
        const auto* payload = RowPayloadCast<OrderEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        EXPECT_NE(payload->event_type, static_cast<int32_t>(OrderEventType::Accepted));
        EXPECT_NE(payload->event_type, static_cast<int32_t>(OrderEventType::Filled));
    }
    EXPECT_TRUE(order_event_rows.empty());
}

TEST_F(InfraLogTestFixture, PositionEventOpenedIsEmittedOnFirstOpen)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::PositionEventDto;
    using QTrading::Log::FileLogger::FeatherV2::PositionEventType;

    WriteBinanceCsv(
        tmp_dir / "position_open.csv",
        { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "position_open.csv").string() } },
            logger,
            1000.0,
            0,
            4500u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto position_event_rows = FilterRowsByModule(QTrading::Log::LogModule::PositionEvent);
    ASSERT_FALSE(position_event_rows.empty());

    const PositionEventDto* opened = nullptr;
    for (const auto& row_view : position_event_rows) {
        const auto* payload = RowPayloadCast<PositionEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->event_type == static_cast<int32_t>(PositionEventType::Opened)) {
            opened = payload;
            break;
        }
    }

    ASSERT_NE(opened, nullptr);
    EXPECT_EQ(opened->symbol, "BTCUSDT");
    EXPECT_TRUE(opened->is_long);
    EXPECT_DOUBLE_EQ(opened->qty, 1.0);
    EXPECT_DOUBLE_EQ(opened->entry_price, 100.0);
}

TEST_F(InfraLogTestFixture, PositionEventIncreasedIsEmittedWhenPositionAddsSize)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::PositionEventDto;
    using QTrading::Log::FileLogger::FeatherV2::PositionEventType;

    WriteBinanceCsv(
        tmp_dir / "position_increase.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 110.0, 110.0, 110.0, 110.0, 1000.0, 90000u, 1100.0, 1, 0.0, 0.0 }
        });

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "position_increase.csv").string() } },
            logger,
            1000.0,
            0,
            4600u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 0.5, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto position_event_rows = FilterRowsByModule(QTrading::Log::LogModule::PositionEvent);
    ASSERT_FALSE(position_event_rows.empty());

    const PositionEventDto* increased = nullptr;
    for (const auto& row_view : position_event_rows) {
        const auto* payload = RowPayloadCast<PositionEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->event_type == static_cast<int32_t>(PositionEventType::Increased)) {
            increased = payload;
            break;
        }
    }

    ASSERT_NE(increased, nullptr);
    EXPECT_EQ(increased->symbol, "BTCUSDT");
    EXPECT_TRUE(increased->is_long);
    EXPECT_DOUBLE_EQ(increased->qty, 1.5);
    EXPECT_GT(increased->entry_price, 100.0);
}

TEST_F(InfraLogTestFixture, PositionEventReducedIsEmittedWhenPositionShrinksWithoutClosing)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::PositionEventDto;
    using QTrading::Log::FileLogger::FeatherV2::PositionEventType;

    WriteBinanceCsv(
        tmp_dir / "position_reduce.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 100.0, 100.0, 100.0, 100.0, 1000.0, 90000u, 1000.0, 1, 0.0, 0.0 }
        });

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "position_reduce.csv").string() } },
            logger,
            1000.0,
            0,
            4700u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        ASSERT_TRUE(exchange.perp.place_order(
            "BTCUSDT",
            0.4,
            100.0,
            OrderSide::Sell,
            PositionSide::Both,
            true));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto position_event_rows = FilterRowsByModule(QTrading::Log::LogModule::PositionEvent);
    ASSERT_FALSE(position_event_rows.empty());

    const PositionEventDto* reduced = nullptr;
    for (const auto& row_view : position_event_rows) {
        const auto* payload = RowPayloadCast<PositionEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->event_type == static_cast<int32_t>(PositionEventType::Reduced)) {
            reduced = payload;
            break;
        }
    }

    ASSERT_NE(reduced, nullptr);
    EXPECT_EQ(reduced->symbol, "BTCUSDT");
    EXPECT_TRUE(reduced->is_long);
    EXPECT_NEAR(reduced->qty, 0.6, 1e-12);
    EXPECT_DOUBLE_EQ(reduced->entry_price, 100.0);
}

TEST_F(InfraLogTestFixture, PositionEventClosedIsEmittedOnFullClose)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::PositionEventDto;
    using QTrading::Log::FileLogger::FeatherV2::PositionEventType;

    WriteBinanceCsv(
        tmp_dir / "position_close.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 100.0, 100.0, 100.0, 100.0, 1000.0, 90000u, 1000.0, 1, 0.0, 0.0 }
        });

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "position_close.csv").string() } },
            logger,
            1000.0,
            0,
            4800u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        exchange.perp.close_position("BTCUSDT");
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto position_event_rows = FilterRowsByModule(QTrading::Log::LogModule::PositionEvent);
    ASSERT_FALSE(position_event_rows.empty());

    const PositionEventDto* closed = nullptr;
    for (const auto& row_view : position_event_rows) {
        const auto* payload = RowPayloadCast<PositionEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->event_type == static_cast<int32_t>(PositionEventType::Closed)) {
            closed = payload;
            break;
        }
    }

    ASSERT_NE(closed, nullptr);
    EXPECT_EQ(closed->symbol, "BTCUSDT");
    EXPECT_TRUE(closed->is_long);
    EXPECT_DOUBLE_EQ(closed->qty, 1.0);
}

TEST_F(InfraLogTestFixture, PositionEventDistinguishesLongAndShortSidesInHedgeMode)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::PositionEventDto;
    using QTrading::Log::FileLogger::FeatherV2::PositionEventType;

    WriteBinanceCsv(
        tmp_dir / "position_hedge.csv",
        { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });

    Account::AccountInitConfig cfg{};
    cfg.perp_initial_wallet = 1000.0;
    auto account = std::make_shared<Account>(cfg);
    account->set_position_mode(true);
    account->set_symbol_leverage("BTCUSDT", 10.0);

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "position_hedge.csv").string() } },
            logger,
            account,
            4900u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy, PositionSide::Long));
        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Sell, PositionSide::Short));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto position_event_rows = FilterRowsByModule(QTrading::Log::LogModule::PositionEvent);
    ASSERT_FALSE(position_event_rows.empty());

    const PositionEventDto* long_opened = nullptr;
    const PositionEventDto* short_opened = nullptr;
    for (const auto& row_view : position_event_rows) {
        const auto* payload = RowPayloadCast<PositionEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->event_type != static_cast<int32_t>(PositionEventType::Opened)) {
            continue;
        }
        if (payload->symbol != "BTCUSDT") {
            continue;
        }
        if (payload->is_long) {
            long_opened = payload;
        }
        else {
            short_opened = payload;
        }
    }

    ASSERT_NE(long_opened, nullptr);
    ASSERT_NE(short_opened, nullptr);
    EXPECT_NE(long_opened->position_id, short_opened->position_id);
    EXPECT_TRUE(long_opened->is_long);
    EXPECT_FALSE(short_opened->is_long);
}

TEST_F(InfraLogTestFixture, PositionEventOneWayReverseKeepsOpenedClosedOpenedLifecycle)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::PositionEventDto;
    using QTrading::Log::FileLogger::FeatherV2::PositionEventType;

    WriteBinanceCsv(
        tmp_dir / "position_reverse.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 100.0, 100.0, 100.0, 100.0, 1000.0, 90000u, 1000.0, 1, 0.0, 0.0 },
            { 120000u, 100.0, 100.0, 100.0, 100.0, 1000.0, 150000u, 1000.0, 1, 0.0, 0.0 }
        });

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "position_reverse.csv").string() } },
            logger,
            1000.0,
            0,
            5000u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 2.0, 100.0, OrderSide::Buy, PositionSide::Both));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 5.0, 100.0, OrderSide::Sell, PositionSide::Both));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto position_event_rows = FilterRowsByModule(QTrading::Log::LogModule::PositionEvent);
    ASSERT_FALSE(position_event_rows.empty());

    std::vector<std::pair<int32_t, bool>> lifecycle;
    for (const auto& row_view : position_event_rows) {
        const auto* payload = RowPayloadCast<PositionEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->event_type == static_cast<int32_t>(PositionEventType::Snapshot)) {
            continue;
        }
        lifecycle.emplace_back(payload->event_type, payload->is_long);
    }

    const std::vector<std::pair<int32_t, bool>> expected{
        { static_cast<int32_t>(PositionEventType::Opened), true },
        { static_cast<int32_t>(PositionEventType::Closed), true },
        { static_cast<int32_t>(PositionEventType::Opened), false }
    };
    EXPECT_EQ(lifecycle, expected);
}

TEST_F(InfraLogTestFixture, AccountEventSpotOpenCarriesCorrectSpotCashAndInventoryDelta)
{
    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::AccountEventDto;
    using QTrading::Log::FileLogger::FeatherV2::AccountEventType;
    using QTrading::Log::FileLogger::FeatherV2::AccountLedger;

    WriteBinanceCsv(
        tmp_dir / "account_event_spot_open.csv",
        { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });

    Account::AccountInitConfig cfg{};
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 0.0;

    {
        BinanceExchange exchange(
            {
                BinanceExchange::SymbolDataset{
                    "BTCUSDT_SPOT",
                    (tmp_dir / "account_event_spot_open.csv").string(),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    InstrumentType::Spot }
            },
            logger,
            cfg,
            5100u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.spot.place_order("BTCUSDT_SPOT", 1.0, 100.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto account_rows = FilterRowsByModule(QTrading::Log::LogModule::AccountEvent);
    ASSERT_EQ(account_rows.size(), 2u);

    const AccountEventDto* fill_account_event = nullptr;
    for (const auto& row_view : account_rows) {
        const auto* payload = RowPayloadCast<AccountEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->request_id != 0u) {
            fill_account_event = payload;
            break;
        }
    }

    ASSERT_NE(fill_account_event, nullptr);
    EXPECT_EQ(fill_account_event->event_type, static_cast<int32_t>(AccountEventType::BalanceSnapshot));
    EXPECT_EQ(fill_account_event->ledger, static_cast<int32_t>(AccountLedger::Spot));
    EXPECT_EQ(fill_account_event->symbol, "BTCUSDT_SPOT");
    EXPECT_NEAR(fill_account_event->spot_cash_delta, -100.1, 1e-12);
    EXPECT_NEAR(fill_account_event->spot_inventory_delta, 1.0, 1e-12);
}

TEST_F(InfraLogTestFixture, AccountEventPerpOpenCarriesCorrectWalletMarginAndAvailableBalances)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::AccountEventDto;
    using QTrading::Log::FileLogger::FeatherV2::AccountLedger;

    WriteBinanceCsv(
        tmp_dir / "account_event_perp_open.csv",
        { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });

    BinanceExchange::StatusSnapshot snap{};
    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "account_event_perp_open.csv").string() } },
            logger,
            1000.0,
            0,
            5200u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        exchange.FillStatusSnapshot(snap);
    }

    StopLogger();

    const auto account_rows = FilterRowsByModule(QTrading::Log::LogModule::AccountEvent);
    ASSERT_EQ(account_rows.size(), 2u);

    const AccountEventDto* fill_account_event = nullptr;
    for (const auto& row_view : account_rows) {
        const auto* payload = RowPayloadCast<AccountEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->request_id != 0u) {
            fill_account_event = payload;
            break;
        }
    }

    ASSERT_NE(fill_account_event, nullptr);
    EXPECT_EQ(fill_account_event->ledger, static_cast<int32_t>(AccountLedger::Perp));
    EXPECT_EQ(fill_account_event->symbol, "BTCUSDT");
    EXPECT_DOUBLE_EQ(fill_account_event->wallet_balance_after, snap.wallet_balance);
    EXPECT_DOUBLE_EQ(fill_account_event->margin_balance_after, snap.margin_balance);
    EXPECT_DOUBLE_EQ(fill_account_event->available_balance_after, snap.available_balance);
}

TEST_F(InfraLogTestFixture, AccountEventFundingStepCarriesCorrectWalletDelta)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::AccountEventDto;
    using QTrading::Log::FileLogger::FeatherV2::AccountLedger;

    WriteBinanceCsv(
        tmp_dir / "funding_trade.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 110.0, 110.0, 110.0, 110.0, 1000.0, 90000u, 1100.0, 1, 0.0, 0.0 }
        });
    WriteBinanceCsv(
        tmp_dir / "funding_mark.csv",
        {
            { 0u, 130.0, 130.0, 130.0, 130.0, 1000.0, 30000u, 1300.0, 1, 0.0, 0.0 },
            { 60000u, 140.0, 140.0, 140.0, 140.0, 1000.0, 90000u, 1400.0, 1, 0.0, 0.0 }
        });
    WriteFundingCsv(
        tmp_dir / "funding.csv",
        { { 60000u, 0.001, std::nullopt } });

    BinanceExchange::StatusSnapshot snap{};
    double after_entry_wallet = 0.0;
    {
        BinanceExchange exchange(
            { BinanceExchange::SymbolDataset{
                "BTCUSDT",
                (tmp_dir / "funding_trade.csv").string(),
                std::optional<std::string>((tmp_dir / "funding.csv").string()),
                (tmp_dir / "funding_mark.csv").string() } },
            logger,
            1000.0,
            0,
            5300u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        exchange.FillStatusSnapshot(snap);
        after_entry_wallet = snap.wallet_balance;

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        exchange.FillStatusSnapshot(snap);
    }

    StopLogger();

    const auto account_rows = FilterRowsByModule(QTrading::Log::LogModule::AccountEvent);
    ASSERT_EQ(account_rows.size(), 3u);

    const auto* funding_account_event = RowPayloadCast<AccountEventDto>(account_rows.back().row);
    ASSERT_NE(funding_account_event, nullptr);
    EXPECT_EQ(funding_account_event->step_seq, 2u);
    EXPECT_EQ(funding_account_event->ledger, static_cast<int32_t>(AccountLedger::Both));
    EXPECT_NEAR(funding_account_event->wallet_delta, -0.14, 1e-6);
    EXPECT_NEAR(funding_account_event->wallet_balance_after - after_entry_wallet, -0.14, 1e-6);
}

TEST_F(InfraLogTestFixture, AccountEventTransfersReflectSpotAndPerpBalancesAfterTransfer)
{
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::AccountEventDto;
    using QTrading::Log::FileLogger::FeatherV2::AccountLedger;

    WriteBinanceCsv(
        tmp_dir / "transfer.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 100.0, 100.0, 100.0, 100.0, 1000.0, 90000u, 1000.0, 1, 0.0, 0.0 },
            { 120000u, 100.0, 100.0, 100.0, 100.0, 1000.0, 150000u, 1000.0, 1, 0.0, 0.0 }
        });

    Account::AccountInitConfig cfg{};
    cfg.spot_initial_cash = 500.0;
    cfg.perp_initial_wallet = 500.0;

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "transfer.csv").string() } },
            logger,
            cfg,
            5400u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        ASSERT_TRUE(exchange.account.transfer_spot_to_perp(100.0));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        ASSERT_TRUE(exchange.account.transfer_perp_to_spot(40.0));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto account_rows = FilterRowsByModule(QTrading::Log::LogModule::AccountEvent);
    ASSERT_EQ(account_rows.size(), 3u);

    const auto* after_spot_to_perp = RowPayloadCast<AccountEventDto>(account_rows[1].row);
    const auto* after_perp_to_spot = RowPayloadCast<AccountEventDto>(account_rows[2].row);
    ASSERT_NE(after_spot_to_perp, nullptr);
    ASSERT_NE(after_perp_to_spot, nullptr);

    EXPECT_EQ(after_spot_to_perp->ledger, static_cast<int32_t>(AccountLedger::Both));
    EXPECT_NEAR(after_spot_to_perp->wallet_delta, 100.0, 1e-12);
    EXPECT_NEAR(after_spot_to_perp->perp_wallet_balance_after, 600.0, 1e-12);
    EXPECT_NEAR(after_spot_to_perp->spot_wallet_balance_after, 400.0, 1e-12);

    EXPECT_EQ(after_perp_to_spot->ledger, static_cast<int32_t>(AccountLedger::Both));
    EXPECT_NEAR(after_perp_to_spot->wallet_delta, -40.0, 1e-12);
    EXPECT_NEAR(after_perp_to_spot->perp_wallet_balance_after, 560.0, 1e-12);
    EXPECT_NEAR(after_perp_to_spot->spot_wallet_balance_after, 440.0, 1e-12);
}

TEST_F(InfraLogTestFixture, AccountEventLiquidationStepReconcilesWalletBalanceChange)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    using QTrading::Dto::Market::Binance::TradeKlineDto;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::AccountEventDto;

    WriteBinanceCsv(
        tmp_dir / "liquidation.csv",
        { { 0u, 1.0, 1.0, 1.0, 1.0, 10000.0, 30000u, 10000.0, 1, 0.0, 0.0 } });
    WriteBinanceCsv(
        tmp_dir / "liquidation_mark.csv",
        { { 0u, 1.0, 1.0, 1.0, 1.0, 10000.0, 30000u, 10000.0, 1, 0.0, 0.0 } });

    BinanceExchange::StatusSnapshot pre_liquidation{};
    BinanceExchange::StatusSnapshot post_liquidation{};
    size_t positions_before_liquidation = 0;
    size_t positions_after_liquidation = 0;
    auto account = std::make_shared<Account>(350000.0, 0);
    account->set_symbol_leverage("BTCUSDT", 75.0);
    ASSERT_TRUE(account->place_order("BTCUSDT", 5000.0, 500.0, OrderSide::Buy, PositionSide::Both));
    TradeKlineDto open_kline{};
    open_kline.OpenPrice = 500.0;
    open_kline.HighPrice = 500.0;
    open_kline.LowPrice = 500.0;
    open_kline.ClosePrice = 500.0;
    open_kline.Volume = 10000.0;
    account->update_positions(
        std::unordered_map<std::string, TradeKlineDto>{ { "BTCUSDT", open_kline } },
        std::unordered_map<std::string, double>{ { "BTCUSDT", 500.0 } });
    ASSERT_FALSE(account->get_all_positions().empty());

    {
        BinanceExchange exchange(
            { BinanceExchange::SymbolDataset{
                "BTCUSDT",
                (tmp_dir / "liquidation.csv").string(),
                std::nullopt,
                (tmp_dir / "liquidation_mark.csv").string() } },
            logger,
            account,
            5500u);
        auto market_channel = exchange.get_market_channel();

        exchange.FillStatusSnapshot(pre_liquidation);
        positions_before_liquidation = exchange.get_all_positions().size();
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        exchange.FillStatusSnapshot(post_liquidation);
        positions_after_liquidation = exchange.get_all_positions().size();
    }

    StopLogger();

    const auto account_rows = FilterRowsByModule(QTrading::Log::LogModule::AccountEvent);
    ASSERT_GE(account_rows.size(), 1u);

    const AccountEventDto* final_step_row = nullptr;
    for (const auto& row_view : account_rows) {
        const auto* payload = RowPayloadCast<AccountEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->step_seq != 1u) {
            continue;
        }
        if (payload->request_id == 0u) {
            final_step_row = payload;
        }
    }

    ASSERT_NE(final_step_row, nullptr);
    EXPECT_GT(positions_before_liquidation, 0u);
    EXPECT_EQ(positions_after_liquidation, 0u);
    EXPECT_DOUBLE_EQ(final_step_row->wallet_balance_after, post_liquidation.wallet_balance);
    EXPECT_DOUBLE_EQ(final_step_row->margin_balance_after, post_liquidation.margin_balance);
}

TEST_F(InfraLogTestFixture, AccountEventAndOrderEventStayConsistentForSameFillFeeAndCashflow)
{
    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::AccountEventDto;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventDto;

    WriteBinanceCsv(
        tmp_dir / "account_order_consistency.csv",
        { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });

    Account::AccountInitConfig cfg{};
    cfg.spot_initial_cash = 1000.0;
    cfg.perp_initial_wallet = 0.0;
    auto account = std::make_shared<Account>(cfg);
    account->set_spot_commission_mode(Account::SpotCommissionMode::BaseOnBuyQuoteOnSell);

    {
        BinanceExchange exchange(
            {
                BinanceExchange::SymbolDataset{
                    "BTCUSDT_SPOT",
                    (tmp_dir / "account_order_consistency.csv").string(),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    InstrumentType::Spot }
            },
            logger,
            account,
            5600u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.spot.place_order("BTCUSDT_SPOT", 1.0, 100.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto account_rows = FilterRowsByModule(QTrading::Log::LogModule::AccountEvent);
    const auto order_rows = FilterRowsByModule(QTrading::Log::LogModule::OrderEvent);
    ASSERT_EQ(account_rows.size(), 2u);
    ASSERT_EQ(order_rows.size(), 1u);

    const auto* account_event = RowPayloadCast<AccountEventDto>(account_rows.front().row);
    const auto* order_event = RowPayloadCast<OrderEventDto>(order_rows.front().row);
    ASSERT_NE(account_event, nullptr);
    ASSERT_NE(order_event, nullptr);

    EXPECT_EQ(account_event->request_id, order_event->request_id);
    EXPECT_EQ(account_event->source_order_id, order_event->order_id);
    EXPECT_EQ(account_event->fee_asset, order_event->fee_asset);
    EXPECT_DOUBLE_EQ(account_event->fee_native, order_event->fee_native);
    EXPECT_DOUBLE_EQ(account_event->fee_quote_equiv, order_event->fee_quote_equiv);
    EXPECT_DOUBLE_EQ(account_event->spot_cash_delta, order_event->spot_cash_delta);
    EXPECT_DOUBLE_EQ(account_event->spot_inventory_delta, order_event->spot_inventory_delta);
    EXPECT_EQ(account_event->commission_model_source, order_event->commission_model_source);
}

TEST_F(InfraLogTestFixture, MarketEventCarriesTradeMarkAndIndexPricesPerSymbol)
{
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::MarketEventDto;

    WriteBinanceCsv(
        tmp_dir / "btc_trade.csv",
        { { 0u, 100.0, 101.0, 99.0, 100.5, 1000.0, 30000u, 1000.0, 1, 10.0, 1000.0 } });
    WriteBinanceCsv(
        tmp_dir / "btc_mark.csv",
        { { 0u, 120.0, 120.0, 120.0, 120.0, 1000.0, 30000u, 1200.0, 1, 0.0, 0.0 } });
    WriteBinanceCsv(
        tmp_dir / "btc_index.csv",
        { { 0u, 110.0, 110.0, 110.0, 110.0, 1000.0, 30000u, 1100.0, 1, 0.0, 0.0 } });

    WriteBinanceCsv(
        tmp_dir / "eth_trade.csv",
        { { 0u, 200.0, 201.0, 199.0, 200.5, 2000.0, 30000u, 2000.0, 1, 20.0, 2000.0 } });
    WriteBinanceCsv(
        tmp_dir / "eth_mark.csv",
        { { 0u, 220.0, 220.0, 220.0, 220.0, 2000.0, 30000u, 2200.0, 1, 0.0, 0.0 } });
    WriteBinanceCsv(
        tmp_dir / "eth_index.csv",
        { { 0u, 210.0, 210.0, 210.0, 210.0, 2000.0, 30000u, 2100.0, 1, 0.0, 0.0 } });

    BinanceExchange::StatusSnapshot snap{};
    {
        BinanceExchange exchange(
            {
                BinanceExchange::SymbolDataset{
                    "BTCUSDT",
                    (tmp_dir / "btc_trade.csv").string(),
                    std::nullopt,
                    (tmp_dir / "btc_mark.csv").string(),
                    (tmp_dir / "btc_index.csv").string() },
                BinanceExchange::SymbolDataset{
                    "ETHUSDT",
                    (tmp_dir / "eth_trade.csv").string(),
                    std::nullopt,
                    (tmp_dir / "eth_mark.csv").string(),
                    (tmp_dir / "eth_index.csv").string() }
            },
            logger,
            1000.0,
            0,
            5700u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        exchange.FillStatusSnapshot(snap);
    }

    StopLogger();

    const auto market_rows = FilterRowsByModule(QTrading::Log::LogModule::MarketEvent);
    ASSERT_EQ(market_rows.size(), 2u);
    ASSERT_EQ(snap.prices.size(), 2u);

    const MarketEventDto* btc = nullptr;
    const MarketEventDto* eth = nullptr;
    for (const auto& row_view : market_rows) {
        const auto* payload = RowPayloadCast<MarketEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->symbol == "BTCUSDT") {
            btc = payload;
        }
        if (payload->symbol == "ETHUSDT") {
            eth = payload;
        }
    }

    ASSERT_NE(btc, nullptr);
    ASSERT_NE(eth, nullptr);
    EXPECT_TRUE(btc->has_kline);
    EXPECT_DOUBLE_EQ(btc->close, 100.5);
    EXPECT_TRUE(btc->has_mark_price);
    EXPECT_DOUBLE_EQ(btc->mark_price, 120.0);
    EXPECT_TRUE(btc->has_index_price);
    EXPECT_DOUBLE_EQ(btc->index_price, 110.0);

    EXPECT_TRUE(eth->has_kline);
    EXPECT_DOUBLE_EQ(eth->close, 200.5);
    EXPECT_TRUE(eth->has_mark_price);
    EXPECT_DOUBLE_EQ(eth->mark_price, 220.0);
    EXPECT_TRUE(eth->has_index_price);
    EXPECT_DOUBLE_EQ(eth->index_price, 210.0);

    EXPECT_DOUBLE_EQ(snap.prices[0].trade_price, btc->close);
    EXPECT_DOUBLE_EQ(snap.prices[0].mark_price, btc->mark_price);
    EXPECT_DOUBLE_EQ(snap.prices[0].index_price, btc->index_price);
    EXPECT_DOUBLE_EQ(snap.prices[1].trade_price, eth->close);
    EXPECT_DOUBLE_EQ(snap.prices[1].mark_price, eth->mark_price);
    EXPECT_DOUBLE_EQ(snap.prices[1].index_price, eth->index_price);
}

TEST_F(InfraLogTestFixture, MarketEventUsesAbsentFlagsForMissingSymbolDataInsteadOfDefaultValues)
{
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::MarketEventDto;

    WriteBinanceCsv(
        tmp_dir / "btc_present.csv",
        { { 0u, 100.0, 101.0, 99.0, 100.5, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });
    WriteBinanceCsv(
        tmp_dir / "eth_missing_at_step.csv",
        { { 60000u, 200.0, 201.0, 199.0, 200.5, 2000.0, 90000u, 2000.0, 1, 0.0, 0.0 } });

    BinanceExchange::StatusSnapshot snap{};
    {
        BinanceExchange exchange(
            {
                { "BTCUSDT", (tmp_dir / "btc_present.csv").string() },
                { "ETHUSDT", (tmp_dir / "eth_missing_at_step.csv").string() }
            },
            logger,
            1000.0,
            0,
            5800u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        exchange.FillStatusSnapshot(snap);
    }

    StopLogger();

    const auto market_rows = FilterRowsByModule(QTrading::Log::LogModule::MarketEvent);
    ASSERT_EQ(market_rows.size(), 2u);

    const MarketEventDto* missing = nullptr;
    for (const auto& row_view : market_rows) {
        const auto* payload = RowPayloadCast<MarketEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->symbol == "ETHUSDT") {
            missing = payload;
            break;
        }
    }

    ASSERT_NE(missing, nullptr);
    EXPECT_FALSE(missing->has_kline);
    EXPECT_FALSE(missing->has_mark_price);
    EXPECT_FALSE(missing->has_index_price);
    EXPECT_DOUBLE_EQ(missing->mark_price, 0.0);
    EXPECT_DOUBLE_EQ(missing->index_price, 0.0);
    ASSERT_EQ(snap.prices.size(), 2u);
    EXPECT_FALSE(snap.prices[1].has_trade_price);
    EXPECT_FALSE(snap.prices[1].has_mark_price);
    EXPECT_FALSE(snap.prices[1].has_index_price);
}

TEST_F(InfraLogTestFixture, MarketEventInterpolatedMarkAndIndexUseInterpolatedSource)
{
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::MarketEventDto;

    WriteBinanceCsv(
        tmp_dir / "interp_trade.csv",
        { { 60000u, 100.0, 101.0, 99.0, 100.5, 1000.0, 90000u, 1000.0, 1, 0.0, 0.0 } });
    WriteBinanceCsv(
        tmp_dir / "interp_mark.csv",
        {
            { 0u, 120.0, 120.0, 120.0, 120.0, 1000.0, 30000u, 1200.0, 1, 0.0, 0.0 },
            { 120000u, 140.0, 140.0, 140.0, 140.0, 1000.0, 150000u, 1400.0, 1, 0.0, 0.0 }
        });
    WriteBinanceCsv(
        tmp_dir / "interp_index.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 120000u, 110.0, 110.0, 110.0, 110.0, 1000.0, 150000u, 1100.0, 1, 0.0, 0.0 }
        });

    {
        BinanceExchange exchange(
            { BinanceExchange::SymbolDataset{
                "BTCUSDT",
                (tmp_dir / "interp_trade.csv").string(),
                std::nullopt,
                (tmp_dir / "interp_mark.csv").string(),
                (tmp_dir / "interp_index.csv").string() } },
            logger,
            1000.0,
            0,
            5900u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto market_rows = FilterRowsByModule(QTrading::Log::LogModule::MarketEvent);
    ASSERT_EQ(market_rows.size(), 1u);
    const auto* market = RowPayloadCast<MarketEventDto>(market_rows.front().row);
    ASSERT_NE(market, nullptr);

    EXPECT_TRUE(market->has_mark_price);
    EXPECT_NEAR(market->mark_price, 130.0, 1e-12);
    EXPECT_EQ(
        market->mark_price_source,
        static_cast<int32_t>(BinanceExchange::ReferencePriceSource::Interpolated));
    EXPECT_TRUE(market->has_index_price);
    EXPECT_NEAR(market->index_price, 105.0, 1e-12);
    EXPECT_EQ(
        market->index_price_source,
        static_cast<int32_t>(BinanceExchange::ReferencePriceSource::Interpolated));
}

TEST_F(InfraLogTestFixture, MarketEventRawMarkAndIndexUseRawSourceWhenPresent)
{
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::MarketEventDto;

    WriteBinanceCsv(
        tmp_dir / "raw_trade.csv",
        { { 0u, 100.0, 101.0, 99.0, 100.5, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });
    WriteBinanceCsv(
        tmp_dir / "raw_mark.csv",
        { { 0u, 123.0, 123.0, 123.0, 123.0, 1000.0, 30000u, 1230.0, 1, 0.0, 0.0 } });
    WriteBinanceCsv(
        tmp_dir / "raw_index.csv",
        { { 0u, 117.0, 117.0, 117.0, 117.0, 1000.0, 30000u, 1170.0, 1, 0.0, 0.0 } });

    {
        BinanceExchange exchange(
            { BinanceExchange::SymbolDataset{
                "BTCUSDT",
                (tmp_dir / "raw_trade.csv").string(),
                std::nullopt,
                (tmp_dir / "raw_mark.csv").string(),
                (tmp_dir / "raw_index.csv").string() } },
            logger,
            1000.0,
            0,
            6000u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto market_rows = FilterRowsByModule(QTrading::Log::LogModule::MarketEvent);
    ASSERT_EQ(market_rows.size(), 1u);
    const auto* market = RowPayloadCast<MarketEventDto>(market_rows.front().row);
    ASSERT_NE(market, nullptr);

    EXPECT_TRUE(market->has_mark_price);
    EXPECT_DOUBLE_EQ(market->mark_price, 123.0);
    EXPECT_EQ(
        market->mark_price_source,
        static_cast<int32_t>(BinanceExchange::ReferencePriceSource::Raw));
    EXPECT_TRUE(market->has_index_price);
    EXPECT_DOUBLE_EQ(market->index_price, 117.0);
    EXPECT_EQ(
        market->index_price_source,
        static_cast<int32_t>(BinanceExchange::ReferencePriceSource::Raw));
}

TEST_F(InfraLogTestFixture, MarketSnapshotUncertaintyBandAndBasisDiagnosticsMatchStatusSnapshot)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::MarketEventDto;

    WriteBinanceCsv(
        tmp_dir / "btc_diag_trade.csv",
        { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });
    WriteBinanceCsv(
        tmp_dir / "btc_diag_mark.csv",
        { { 0u, 120.0, 120.0, 120.0, 120.0, 1000.0, 30000u, 1200.0, 1, 0.0, 0.0 } });
    WriteBinanceCsv(
        tmp_dir / "btc_diag_index.csv",
        { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });

    WriteBinanceCsv(
        tmp_dir / "eth_diag_trade.csv",
        { { 0u, 200.0, 200.0, 200.0, 200.0, 1000.0, 30000u, 2000.0, 1, 0.0, 0.0 } });
    WriteBinanceCsv(
        tmp_dir / "eth_diag_mark.csv",
        { { 0u, 212.0, 212.0, 212.0, 212.0, 1000.0, 30000u, 2120.0, 1, 0.0, 0.0 } });
    WriteBinanceCsv(
        tmp_dir / "eth_diag_index.csv",
        { { 0u, 200.0, 200.0, 200.0, 200.0, 1000.0, 30000u, 2000.0, 1, 0.0, 0.0 } });

    BinanceExchange::StatusSnapshot snap{};
    {
        BinanceExchange exchange(
            {
                BinanceExchange::SymbolDataset{
                    "BTCUSDT",
                    (tmp_dir / "btc_diag_trade.csv").string(),
                    std::nullopt,
                    (tmp_dir / "btc_diag_mark.csv").string(),
                    (tmp_dir / "btc_diag_index.csv").string() },
                BinanceExchange::SymbolDataset{
                    "ETHUSDT",
                    (tmp_dir / "eth_diag_trade.csv").string(),
                    std::nullopt,
                    (tmp_dir / "eth_diag_mark.csv").string(),
                    (tmp_dir / "eth_diag_index.csv").string() }
            },
            logger,
            1000.0,
            0,
            6100u);
        exchange.set_uncertainty_band_bps(20.0);
        exchange.set_mark_index_basis_thresholds_bps(500.0, 1500.0);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        EXPECT_FALSE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        EXPECT_TRUE(exchange.perp.place_order("ETHUSDT", 1.0, OrderSide::Buy));
        exchange.FillStatusSnapshot(snap);
    }

    StopLogger();

    const auto market_rows = FilterRowsByModule(QTrading::Log::LogModule::MarketEvent);
    ASSERT_EQ(market_rows.size(), 2u);
    EXPECT_EQ(snap.basis_warning_symbols, 1u);
    EXPECT_EQ(snap.basis_stress_symbols, 1u);
    EXPECT_EQ(snap.basis_stress_blocked_orders, 1u);
    EXPECT_NEAR(snap.uncertainty_band_bps, 2020.0, 1e-9);
    const double band = snap.uncertainty_band_bps / 10000.0;
    EXPECT_NEAR(snap.total_ledger_value_conservative, snap.total_ledger_value_base * (1.0 - band), 1e-9);
    EXPECT_NEAR(snap.total_ledger_value_optimistic, snap.total_ledger_value_base * (1.0 + band), 1e-9);

    const MarketEventDto* btc = nullptr;
    const MarketEventDto* eth = nullptr;
    for (const auto& row_view : market_rows) {
        const auto* payload = RowPayloadCast<MarketEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->symbol == "BTCUSDT") {
            btc = payload;
        }
        if (payload->symbol == "ETHUSDT") {
            eth = payload;
        }
    }
    ASSERT_NE(btc, nullptr);
    ASSERT_NE(eth, nullptr);
    EXPECT_DOUBLE_EQ(btc->mark_price, 120.0);
    EXPECT_DOUBLE_EQ(btc->index_price, 100.0);
    EXPECT_DOUBLE_EQ(eth->mark_price, 212.0);
    EXPECT_DOUBLE_EQ(eth->index_price, 200.0);
}

TEST_F(InfraLogTestFixture, FundingEventIsEmittedWhenFundingCsvHasApplicableRows)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::FundingEventDto;

    WriteBinanceCsv(
        tmp_dir / "funding_emitted_trade.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 110.0, 110.0, 110.0, 110.0, 1000.0, 90000u, 1100.0, 1, 0.0, 0.0 }
        });
    WriteFundingCsv(
        tmp_dir / "funding_emitted.csv",
        { { 60000u, 0.001, 100.0 } });

    {
        BinanceExchange exchange(
            { BinanceExchange::SymbolDataset{
                "BTCUSDT",
                (tmp_dir / "funding_emitted_trade.csv").string(),
                std::optional<std::string>((tmp_dir / "funding_emitted.csv").string()) } },
            logger,
            1000.0,
            0,
            6200u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto funding_rows = FilterRowsByModule(QTrading::Log::LogModule::FundingEvent);
    ASSERT_EQ(funding_rows.size(), 1u);
    const auto* payload = RowPayloadCast<FundingEventDto>(funding_rows.front().row);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->symbol, "BTCUSDT");
    EXPECT_EQ(payload->funding_time, 60000u);
    EXPECT_EQ(payload->skip_reason, 0);
}

TEST_F(InfraLogTestFixture, FundingEventExplicitMarkPriceCarriesCorrectRateMarkAndFundingValue)
{
    using QTrading::Dto::Trading::InstrumentType;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::FundingEventDto;

    WriteBinanceCsv(
        tmp_dir / "funding_explicit_trade.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 110.0, 110.0, 110.0, 110.0, 1000.0, 90000u, 1100.0, 1, 0.0, 0.0 }
        });
    WriteFundingCsv(
        tmp_dir / "funding_explicit.csv",
        { { 60000u, 0.001, 140.0 } });

    {
        BinanceExchange exchange(
            { BinanceExchange::SymbolDataset{
                "BTCUSDT",
                (tmp_dir / "funding_explicit_trade.csv").string(),
                std::optional<std::string>((tmp_dir / "funding_explicit.csv").string()) } },
            logger,
            1000.0,
            0,
            6300u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto funding_rows = FilterRowsByModule(QTrading::Log::LogModule::FundingEvent);
    ASSERT_EQ(funding_rows.size(), 1u);
    const auto* payload = RowPayloadCast<FundingEventDto>(funding_rows.front().row);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->instrument_type, static_cast<int32_t>(InstrumentType::Perp));
    EXPECT_EQ(payload->rate, 0.001);
    EXPECT_TRUE(payload->has_mark_price);
    EXPECT_DOUBLE_EQ(payload->mark_price, 140.0);
    EXPECT_EQ(
        payload->mark_price_source,
        static_cast<int32_t>(BinanceExchange::ReferencePriceSource::Raw));
    EXPECT_TRUE(payload->is_long);
    EXPECT_DOUBLE_EQ(payload->quantity, 1.0);
    EXPECT_NEAR(payload->quantity * payload->mark_price, 140.0, 1e-12);
    EXPECT_NEAR(payload->funding, -0.14, 1e-12);
}

TEST_F(InfraLogTestFixture, FundingEventInterpolatedMarkPriceRemainsCorrectAndSourceIsInterpolated)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::FundingEventDto;

    WriteBinanceCsv(
        tmp_dir / "funding_interpolated_trade.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 120000u, 120.0, 120.0, 120.0, 120.0, 1000.0, 150000u, 1200.0, 1, 0.0, 0.0 }
        });
    WriteBinanceCsv(
        tmp_dir / "funding_interpolated_mark.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 120000u, 120.0, 120.0, 120.0, 120.0, 1000.0, 150000u, 1200.0, 1, 0.0, 0.0 }
        });
    WriteFundingCsv(
        tmp_dir / "funding_interpolated.csv",
        { { 60000u, 0.001, std::nullopt } });

    {
        BinanceExchange exchange(
            { BinanceExchange::SymbolDataset{
                "BTCUSDT",
                (tmp_dir / "funding_interpolated_trade.csv").string(),
                std::optional<std::string>((tmp_dir / "funding_interpolated.csv").string()),
                (tmp_dir / "funding_interpolated_mark.csv").string() } },
            logger,
            1000.0,
            0,
            6400u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto funding_rows = FilterRowsByModule(QTrading::Log::LogModule::FundingEvent);
    ASSERT_EQ(funding_rows.size(), 1u);
    const auto* payload = RowPayloadCast<FundingEventDto>(funding_rows.front().row);
    ASSERT_NE(payload, nullptr);
    EXPECT_TRUE(payload->has_mark_price);
    EXPECT_NEAR(payload->mark_price, 110.0, 1e-12);
    EXPECT_EQ(
        payload->mark_price_source,
        static_cast<int32_t>(BinanceExchange::ReferencePriceSource::Interpolated));
    EXPECT_NEAR(payload->funding, -0.11, 1e-12);
}

TEST_F(InfraLogTestFixture, FundingEventTimingBeforeAndAfterMatchingProduceExpectedDifferences)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::FundingEventDto;

    WriteBinanceCsv(
        tmp_dir / "funding_timing_trade.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 200.0, 200.0, 200.0, 200.0, 1000.0, 90000u, 2000.0, 1, 0.0, 0.0 }
        });
    WriteFundingCsv(
        tmp_dir / "funding_timing.csv",
        { { 60000u, 0.001, 100.0 } });

    double before_wallet = 0.0;
    double after_wallet = 0.0;
    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "funding_timing_trade.csv").string(),
                std::optional<std::string>((tmp_dir / "funding_timing.csv").string()) } },
            logger,
            1000.0,
            0,
            6500u);
        exchange.set_funding_apply_timing(BinanceExchange::FundingApplyTiming::BeforeMatching);
        auto market_channel = exchange.get_market_channel();
        BinanceExchange::StatusSnapshot snap{};

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        exchange.FillStatusSnapshot(snap);
        before_wallet = snap.wallet_balance;
    }

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "funding_timing_trade.csv").string(),
                std::optional<std::string>((tmp_dir / "funding_timing.csv").string()) } },
            logger,
            1000.0,
            0,
            6501u);
        exchange.set_funding_apply_timing(BinanceExchange::FundingApplyTiming::AfterMatching);
        auto market_channel = exchange.get_market_channel();
        BinanceExchange::StatusSnapshot snap{};

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        exchange.FillStatusSnapshot(snap);
        after_wallet = snap.wallet_balance;
    }

    StopLogger();

    const auto funding_rows = FilterRowsByModule(QTrading::Log::LogModule::FundingEvent);
    std::vector<FundingEventDto> before_events;
    std::vector<FundingEventDto> after_events;
    for (const auto& row_view : funding_rows) {
        const auto* payload = RowPayloadCast<FundingEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->run_id == 6500u) {
            before_events.push_back(*payload);
        }
        if (payload->run_id == 6501u) {
            after_events.push_back(*payload);
        }
    }

    ASSERT_EQ(before_events.size(), 1u);
    ASSERT_EQ(after_events.size(), 1u);
    EXPECT_EQ(before_events.front().funding_time, 60000u);
    EXPECT_EQ(after_events.front().funding_time, 60000u);
    EXPECT_NEAR(before_events.front().quantity, 1.0, 1e-12);
    EXPECT_NEAR(after_events.front().quantity, 2.0, 1e-12);
    EXPECT_NEAR(before_events.front().funding, -0.1, 1e-12);
    EXPECT_NEAR(after_events.front().funding, -0.2, 1e-12);
    EXPECT_NEAR(before_wallet - after_wallet, 0.1, 1e-6);
}

TEST_F(InfraLogTestFixture, FundingSkippedNoMarkStatisticsStayConsistentWithFundingEvents)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::FundingEventDto;

    WriteBinanceCsv(
        tmp_dir / "funding_skipped_trade.csv",
        { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });
    WriteFundingCsv(
        tmp_dir / "funding_skipped.csv",
        {
            { 30000u, 0.001, std::nullopt },
            { 600000u, 0.002, std::nullopt }
        });

    BinanceExchange::StatusSnapshot snap{};
    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "funding_skipped_trade.csv").string(),
                std::optional<std::string>((tmp_dir / "funding_skipped.csv").string()) } },
            logger,
            1000.0,
            0,
            6600u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        exchange.FillStatusSnapshot(snap);
    }

    StopLogger();

    const auto funding_rows = FilterRowsByModule(QTrading::Log::LogModule::FundingEvent);
    ASSERT_EQ(funding_rows.size(), 2u);
    for (const auto& row_view : funding_rows) {
        const auto* payload = RowPayloadCast<FundingEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        EXPECT_EQ(payload->skip_reason, 1);
        EXPECT_FALSE(payload->has_mark_price);
        EXPECT_DOUBLE_EQ(payload->funding, 0.0);
    }
    EXPECT_EQ(snap.funding_skipped_no_mark, 2u);
    EXPECT_EQ(snap.funding_applied_events, 0u);
}

TEST_F(InfraLogTestFixture, AsyncAckLatencyPublishesPendingBeforeAcceptedOrRejected)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    WriteBinanceCsv(
        tmp_dir / "ack_accepted.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 100.0, 30000u, 100.0, 1, 0.0, 0.0 },
            { 60000u, 100.0, 100.0, 100.0, 100.0, 100.0, 90000u, 100.0, 1, 0.0, 0.0 }
        });
    WriteBinanceCsv(
        tmp_dir / "ack_rejected.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 100.0, 30000u, 100.0, 1, 0.0, 0.0 },
            { 60000u, 100.0, 100.0, 100.0, 100.0, 100.0, 90000u, 100.0, 1, 0.0, 0.0 }
        });

    BinanceExchange::AsyncOrderAck accepted_pending{};
    BinanceExchange::AsyncOrderAck accepted_resolved{};
    BinanceExchange::AsyncOrderAck rejected_pending{};
    BinanceExchange::AsyncOrderAck rejected_resolved{};

    {
        BinanceExchange exchange(
            {
                { "BTCUSDT", (tmp_dir / "ack_accepted.csv").string() },
                { "ETHUSDT", (tmp_dir / "ack_rejected.csv").string() }
            },
            logger,
            1000.0,
            0,
            6700u);
        exchange.set_order_latency_bars(1);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        ASSERT_TRUE(exchange.perp.place_order(
            "BTCUSDT",
            1.0,
            50.0,
            OrderSide::Buy,
            PositionSide::Both,
            false,
            "cid-accepted"));
        ASSERT_TRUE(exchange.spot.place_order("ETHUSDT", 1.0, 100.0, OrderSide::Sell));

        auto pending_acks = exchange.drain_async_order_acks();
        ASSERT_EQ(pending_acks.size(), 2u);
        for (const auto& ack : pending_acks) {
            EXPECT_EQ(ack.status, BinanceExchange::AsyncOrderAck::Status::Pending);
            EXPECT_EQ(ack.submitted_step, 1u);
            EXPECT_EQ(ack.due_step, 2u);
            EXPECT_EQ(ack.resolved_step, 0u);
            if (ack.symbol == "BTCUSDT") {
                accepted_pending = ack;
            }
            if (ack.symbol == "ETHUSDT") {
                rejected_pending = ack;
            }
        }

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        auto resolved_acks = exchange.drain_async_order_acks();
        ASSERT_EQ(resolved_acks.size(), 2u);
        for (const auto& ack : resolved_acks) {
            EXPECT_EQ(ack.submitted_step, 1u);
            EXPECT_EQ(ack.due_step, 2u);
            EXPECT_EQ(ack.resolved_step, 2u);
            if (ack.symbol == "BTCUSDT") {
                accepted_resolved = ack;
            }
            if (ack.symbol == "ETHUSDT") {
                rejected_resolved = ack;
            }
        }
    }

    StopLogger();

    EXPECT_EQ(accepted_pending.status, BinanceExchange::AsyncOrderAck::Status::Pending);
    EXPECT_EQ(accepted_resolved.status, BinanceExchange::AsyncOrderAck::Status::Accepted);
    EXPECT_EQ(accepted_resolved.request_id, accepted_pending.request_id);

    EXPECT_EQ(rejected_pending.status, BinanceExchange::AsyncOrderAck::Status::Pending);
    EXPECT_EQ(rejected_resolved.status, BinanceExchange::AsyncOrderAck::Status::Rejected);
    EXPECT_EQ(rejected_resolved.request_id, rejected_pending.request_id);
}

TEST_F(InfraLogTestFixture, PendingAsyncAckDoesNotEmitAcceptedOrderEventEarly)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventDto;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventType;

    WriteBinanceCsv(
        tmp_dir / "pending_no_event.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 100.0, 30000u, 100.0, 1, 0.0, 0.0 },
            { 60000u, 100.0, 100.0, 100.0, 100.0, 100.0, 90000u, 100.0, 1, 0.0, 0.0 }
        });

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "pending_no_event.csv").string() } },
            logger,
            1000.0,
            0,
            6800u);
        exchange.set_order_latency_bars(1);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        ASSERT_TRUE(exchange.perp.place_order(
            "BTCUSDT",
            1.0,
            50.0,
            OrderSide::Buy,
            PositionSide::Both,
            false,
            "cid-pending-no-event"));

        auto pending_acks = exchange.drain_async_order_acks();
        ASSERT_EQ(pending_acks.size(), 1u);
        ASSERT_EQ(pending_acks.front().status, BinanceExchange::AsyncOrderAck::Status::Pending);

        const auto pending_phase_order_events = FilterRowsByModule(QTrading::Log::LogModule::OrderEvent);
        EXPECT_TRUE(pending_phase_order_events.empty());

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto order_event_rows = FilterRowsByModule(QTrading::Log::LogModule::OrderEvent);
    ASSERT_EQ(order_event_rows.size(), 1u);
    const auto* payload = RowPayloadCast<OrderEventDto>(order_event_rows.front().row);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->event_type, static_cast<int32_t>(OrderEventType::Accepted));
    EXPECT_EQ(payload->step_seq, 2u);
}

TEST_F(InfraLogTestFixture, RejectedAsyncAckCarriesRejectAndBinanceErrorDetails)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    WriteBinanceCsv(
        tmp_dir / "ack_reject_detail.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 100.0, 30000u, 100.0, 1, 0.0, 0.0 },
            { 60000u, 100.0, 100.0, 100.0, 100.0, 100.0, 90000u, 100.0, 1, 0.0, 0.0 }
        });

    BinanceExchange::AsyncOrderAck rejected_ack{};
    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "ack_reject_detail.csv").string() } },
            logger,
            1000.0,
            0,
            6900u);
        exchange.set_order_latency_bars(1);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        ASSERT_TRUE(exchange.spot.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Sell));
        auto pending = exchange.drain_async_order_acks();
        ASSERT_EQ(pending.size(), 1u);
        ASSERT_EQ(pending.front().status, BinanceExchange::AsyncOrderAck::Status::Pending);

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        auto rejected = exchange.drain_async_order_acks();
        ASSERT_EQ(rejected.size(), 1u);
        rejected_ack = rejected.front();
    }

    StopLogger();

    EXPECT_EQ(rejected_ack.status, BinanceExchange::AsyncOrderAck::Status::Rejected);
    EXPECT_EQ(rejected_ack.reject_code, Contracts::OrderRejectInfo::Code::SpotNoInventory);
    EXPECT_FALSE(rejected_ack.reject_message.empty());
    EXPECT_EQ(rejected_ack.binance_error_code, -2010);
    EXPECT_FALSE(rejected_ack.binance_error_message.empty());
}

TEST_F(InfraLogTestFixture, AcceptedAsyncAckAppearsBeforeSnapshotAndEventVisibility)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    WriteBinanceCsv(
        tmp_dir / "ack_snapshot_visibility.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 100.0, 30000u, 100.0, 1, 0.0, 0.0 },
            { 60000u, 100.0, 100.0, 100.0, 100.0, 100.0, 90000u, 100.0, 1, 0.0, 0.0 }
        });

    BinanceExchange::AsyncOrderAck accepted_ack{};
    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "ack_snapshot_visibility.csv").string() } },
            logger,
            1000.0,
            0,
            7000u);
        exchange.set_order_latency_bars(1);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        ASSERT_TRUE(exchange.perp.place_order(
            "BTCUSDT",
            1.0,
            50.0,
            OrderSide::Buy,
            PositionSide::Both,
            false,
            "cid-accepted-visibility"));

        auto pending = exchange.drain_async_order_acks();
        ASSERT_EQ(pending.size(), 1u);
        ASSERT_EQ(pending.front().status, BinanceExchange::AsyncOrderAck::Status::Pending);
        EXPECT_TRUE(FilterRowsByModule(QTrading::Log::LogModule::Order).empty());
        EXPECT_TRUE(FilterRowsByModule(QTrading::Log::LogModule::OrderEvent).empty());

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        auto resolved = exchange.drain_async_order_acks();
        ASSERT_EQ(resolved.size(), 1u);
        accepted_ack = resolved.front();
        ASSERT_EQ(accepted_ack.status, BinanceExchange::AsyncOrderAck::Status::Accepted);
    }

    StopLogger();

    EXPECT_EQ(accepted_ack.resolved_step, 2u);
    const auto order_rows = FilterRowsByModule(QTrading::Log::LogModule::Order);
    const auto order_event_rows = FilterRowsByModule(QTrading::Log::LogModule::OrderEvent);
    ASSERT_EQ(order_rows.size(), 1u);
    ASSERT_EQ(order_event_rows.size(), 1u);
}

TEST_F(InfraLogTestFixture, StatusSnapshotsAreEmittedBeforeSameStepEvents)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::AccountEventDto;
    using QTrading::Log::FileLogger::FeatherV2::FundingEventDto;
    using QTrading::Log::FileLogger::FeatherV2::MarketEventDto;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventDto;
    using QTrading::Log::FileLogger::FeatherV2::PositionEventDto;

    SCOPED_TRACE(::testing::Message()
        << "baseline_pin id=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kStatusSnapshotsBeforeEvents.id
        << " run_id=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kStatusSnapshotsBeforeEvents.run_id
        << " baseline_input_version=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kBaselineInputVersion
        << " seed=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kPinnedDeterministicSeed
        << " scenario=status_snapshots_before_same_step_events");
    WriteBinanceCsv(
        tmp_dir / QTrading::Infra::Tests::BaselineSemanticsPinning::kStatusSnapshotsBeforeEventsTradeCsv,
        { { 0u, 100.0, 100.0, 100.0, 100.0, 100.0, 30000u, 100.0, 1, 0.0, 0.0 } });

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / QTrading::Infra::Tests::BaselineSemanticsPinning::kStatusSnapshotsBeforeEventsTradeCsv).string() } },
            logger,
            1000.0,
            0,
            QTrading::Infra::Tests::BaselineSemanticsPinning::kStatusSnapshotsBeforeEvents.run_id);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, 90.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto account_snapshot_rows = FilterRowsByModule(QTrading::Log::LogModule::Account);
    const auto position_snapshot_rows = FilterRowsByModule(QTrading::Log::LogModule::Position);
    const auto order_snapshot_rows = FilterRowsByModule(QTrading::Log::LogModule::Order);
    ASSERT_FALSE(account_snapshot_rows.empty());

    std::vector<size_t> snapshot_arrivals;
    for (const auto& row : account_snapshot_rows) {
        snapshot_arrivals.push_back(row.arrival_index);
    }
    for (const auto& row : position_snapshot_rows) {
        snapshot_arrivals.push_back(row.arrival_index);
    }
    for (const auto& row : order_snapshot_rows) {
        snapshot_arrivals.push_back(row.arrival_index);
    }
    ASSERT_FALSE(snapshot_arrivals.empty());

    std::vector<size_t> step1_event_arrivals;
    for (const auto& row : FilterRowsByModule(QTrading::Log::LogModule::MarketEvent)) {
        const auto* payload = RowPayloadCast<MarketEventDto>(row.row);
        ASSERT_NE(payload, nullptr);
        if (payload->step_seq == 1u) {
            step1_event_arrivals.push_back(row.arrival_index);
        }
    }
    for (const auto& row : FilterRowsByModule(QTrading::Log::LogModule::FundingEvent)) {
        const auto* payload = RowPayloadCast<FundingEventDto>(row.row);
        ASSERT_NE(payload, nullptr);
        if (payload->step_seq == 1u) {
            step1_event_arrivals.push_back(row.arrival_index);
        }
    }
    for (const auto& row : FilterRowsByModule(QTrading::Log::LogModule::AccountEvent)) {
        const auto* payload = RowPayloadCast<AccountEventDto>(row.row);
        ASSERT_NE(payload, nullptr);
        if (payload->step_seq == 1u) {
            step1_event_arrivals.push_back(row.arrival_index);
        }
    }
    for (const auto& row : FilterRowsByModule(QTrading::Log::LogModule::PositionEvent)) {
        const auto* payload = RowPayloadCast<PositionEventDto>(row.row);
        ASSERT_NE(payload, nullptr);
        if (payload->step_seq == 1u) {
            step1_event_arrivals.push_back(row.arrival_index);
        }
    }
    for (const auto& row : FilterRowsByModule(QTrading::Log::LogModule::OrderEvent)) {
        const auto* payload = RowPayloadCast<OrderEventDto>(row.row);
        ASSERT_NE(payload, nullptr);
        if (payload->step_seq == 1u) {
            step1_event_arrivals.push_back(row.arrival_index);
        }
    }

    ASSERT_FALSE(step1_event_arrivals.empty());
    const auto last_snapshot_arrival = *std::max_element(snapshot_arrivals.begin(), snapshot_arrivals.end());
    const auto first_event_arrival = *std::min_element(step1_event_arrivals.begin(), step1_event_arrivals.end());
    EXPECT_LT(last_snapshot_arrival, first_event_arrival);
}

TEST_F(InfraLogTestFixture, EventModulesPreserveMarketFundingAccountPositionOrderOrdering)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::AccountEventDto;
    using QTrading::Log::FileLogger::FeatherV2::FundingEventDto;
    using QTrading::Log::FileLogger::FeatherV2::MarketEventDto;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventDto;
    using QTrading::Log::FileLogger::FeatherV2::PositionEventDto;

    SCOPED_TRACE(::testing::Message()
        << "baseline_pin id=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kEventModuleOrdering.id
        << " run_id=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kEventModuleOrdering.run_id
        << " baseline_input_version=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kBaselineInputVersion
        << " seed=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kPinnedDeterministicSeed
        << " scenario=event_module_ordering_market_funding_account_position_order");
    WriteBinanceCsv(
        tmp_dir / QTrading::Infra::Tests::BaselineSemanticsPinning::kEventModuleOrderingTradeCsv,
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 101.0, 101.0, 101.0, 101.0, 1000.0, 90000u, 1010.0, 1, 0.0, 0.0 }
        });
    WriteFundingCsv(
        tmp_dir / QTrading::Infra::Tests::BaselineSemanticsPinning::kEventModuleOrderingFundingCsv,
        { { 60000u, 0.0001, 101.0 } });

    {
        BinanceExchange exchange(
            { BinanceExchange::SymbolDataset{
                "BTCUSDT",
                (tmp_dir / QTrading::Infra::Tests::BaselineSemanticsPinning::kEventModuleOrderingTradeCsv).string(),
                (tmp_dir / QTrading::Infra::Tests::BaselineSemanticsPinning::kEventModuleOrderingFundingCsv).string() } },
            logger,
            1000.0,
            0,
            QTrading::Infra::Tests::BaselineSemanticsPinning::kEventModuleOrdering.run_id);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 0.5, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    std::vector<ArrivedRowView> market_rows;
    std::vector<ArrivedRowView> funding_rows;
    std::vector<ArrivedRowView> account_rows;
    std::vector<ArrivedRowView> position_rows;
    std::vector<ArrivedRowView> order_rows;
    for (const auto& row_view : DrainAndSortRowsByArrival()) {
        if (row_view.row->module_id == ModuleId(QTrading::Log::LogModule::MarketEvent)) {
            const auto* payload = RowPayloadCast<MarketEventDto>(row_view.row);
            ASSERT_NE(payload, nullptr);
            if (payload->step_seq == 2u) {
                market_rows.push_back(row_view);
            }
        }
        else if (row_view.row->module_id == ModuleId(QTrading::Log::LogModule::FundingEvent)) {
            const auto* payload = RowPayloadCast<FundingEventDto>(row_view.row);
            ASSERT_NE(payload, nullptr);
            if (payload->step_seq == 2u) {
                funding_rows.push_back(row_view);
            }
        }
        else if (row_view.row->module_id == ModuleId(QTrading::Log::LogModule::AccountEvent)) {
            const auto* payload = RowPayloadCast<AccountEventDto>(row_view.row);
            ASSERT_NE(payload, nullptr);
            if (payload->step_seq == 2u) {
                account_rows.push_back(row_view);
            }
        }
        else if (row_view.row->module_id == ModuleId(QTrading::Log::LogModule::PositionEvent)) {
            const auto* payload = RowPayloadCast<PositionEventDto>(row_view.row);
            ASSERT_NE(payload, nullptr);
            if (payload->step_seq == 2u) {
                position_rows.push_back(row_view);
            }
        }
        else if (row_view.row->module_id == ModuleId(QTrading::Log::LogModule::OrderEvent)) {
            const auto* payload = RowPayloadCast<OrderEventDto>(row_view.row);
            ASSERT_NE(payload, nullptr);
            if (payload->step_seq == 2u) {
                order_rows.push_back(row_view);
            }
        }
    }

    ASSERT_EQ(market_rows.size(), 1u);
    ASSERT_EQ(funding_rows.size(), 1u);
    ASSERT_FALSE(account_rows.empty());
    ASSERT_FALSE(position_rows.empty());
    ASSERT_FALSE(order_rows.empty());
    EXPECT_LT(market_rows.back().arrival_index, funding_rows.front().arrival_index);
    EXPECT_LT(funding_rows.back().arrival_index, account_rows.front().arrival_index);
    EXPECT_LT(account_rows.back().arrival_index, position_rows.front().arrival_index);
    EXPECT_LT(position_rows.back().arrival_index, order_rows.front().arrival_index);
}

TEST_F(InfraLogTestFixture, AsyncAckPendingResolvedAndRejectMappingStayStable)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventDto;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventType;

    SCOPED_TRACE(::testing::Message()
        << "baseline_pin id=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kAsyncAckRejectMapping.id
        << " run_id=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kAsyncAckRejectMapping.run_id
        << " baseline_input_version=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kBaselineInputVersion
        << " seed=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kPinnedDeterministicSeed
        << " scenario=async_ack_pending_resolved_reject_mapping");
    WriteBinanceCsv(
        tmp_dir / QTrading::Infra::Tests::BaselineSemanticsPinning::kAsyncAckRejectMappingTradeCsv,
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 100.0, 30000u, 100.0, 1, 0.0, 0.0 },
            { 60000u, 100.0, 100.0, 100.0, 100.0, 100.0, 90000u, 100.0, 1, 0.0, 0.0 }
        });

    BinanceExchange::AsyncOrderAck pending_ack{};
    BinanceExchange::AsyncOrderAck rejected_ack{};
    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / QTrading::Infra::Tests::BaselineSemanticsPinning::kAsyncAckRejectMappingTradeCsv).string() } },
            logger,
            1000.0,
            0,
            QTrading::Infra::Tests::BaselineSemanticsPinning::kAsyncAckRejectMapping.run_id);
        exchange.set_order_latency_bars(1);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        ASSERT_TRUE(exchange.spot.place_order("BTCUSDT", 1.0, 100.0, OrderSide::Sell));

        auto pending = exchange.drain_async_order_acks();
        ASSERT_EQ(pending.size(), 1u);
        pending_ack = pending.front();

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        auto resolved = exchange.drain_async_order_acks();
        ASSERT_EQ(resolved.size(), 1u);
        rejected_ack = resolved.front();
    }

    StopLogger();

    ASSERT_EQ(pending_ack.status, BinanceExchange::AsyncOrderAck::Status::Pending);
    EXPECT_EQ(pending_ack.submitted_step, 1u);
    EXPECT_EQ(pending_ack.due_step, 2u);
    EXPECT_EQ(pending_ack.resolved_step, 0u);

    ASSERT_EQ(rejected_ack.status, BinanceExchange::AsyncOrderAck::Status::Rejected);
    EXPECT_EQ(rejected_ack.request_id, pending_ack.request_id);
    EXPECT_EQ(rejected_ack.submitted_step, pending_ack.submitted_step);
    EXPECT_EQ(rejected_ack.due_step, pending_ack.due_step);
    EXPECT_EQ(rejected_ack.resolved_step, 2u);
    EXPECT_EQ(rejected_ack.reject_code, Contracts::OrderRejectInfo::Code::SpotNoInventory);
    EXPECT_EQ(rejected_ack.binance_error_code, -2010);
    EXPECT_FALSE(rejected_ack.binance_error_message.empty());

    for (const auto& row_view : FilterRowsByModule(QTrading::Log::LogModule::OrderEvent)) {
        const auto* payload = RowPayloadCast<OrderEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        EXPECT_NE(payload->event_type, static_cast<int32_t>(OrderEventType::Accepted));
        EXPECT_NE(payload->event_type, static_cast<int32_t>(OrderEventType::Filled));
    }
}

TEST_F(InfraLogTestFixture, FundingAndFillSameStepBeforeAfterMatchingRemainStable)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::FundingEventDto;

    SCOPED_TRACE(::testing::Message()
        << "baseline_pin id=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kFundingFillSameStepBeforeMatching.id
        << " run_id=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kFundingFillSameStepBeforeMatching.run_id
        << " baseline_input_version=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kBaselineInputVersion
        << " seed=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kPinnedDeterministicSeed
        << " scenario=funding_fill_same_step_before_matching");
    SCOPED_TRACE(::testing::Message()
        << "baseline_pin id=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kFundingFillSameStepAfterMatching.id
        << " run_id=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kFundingFillSameStepAfterMatching.run_id
        << " baseline_input_version=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kBaselineInputVersion
        << " seed=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kPinnedDeterministicSeed
        << " scenario=funding_fill_same_step_after_matching");
    WriteBinanceCsv(
        tmp_dir / QTrading::Infra::Tests::BaselineSemanticsPinning::kFundingFillSameStepTradeCsv,
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 200.0, 200.0, 200.0, 200.0, 1000.0, 90000u, 2000.0, 1, 0.0, 0.0 }
        });
    WriteFundingCsv(
        tmp_dir / QTrading::Infra::Tests::BaselineSemanticsPinning::kFundingFillSameStepFundingCsv,
        { { 60000u, 0.001, 100.0 } });

    auto run_timing = [&](BinanceExchange::FundingApplyTiming timing, uint64_t run_id) -> double {
        BinanceExchange::StatusSnapshot snap{};
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / QTrading::Infra::Tests::BaselineSemanticsPinning::kFundingFillSameStepTradeCsv).string(),
                std::optional<std::string>((tmp_dir / QTrading::Infra::Tests::BaselineSemanticsPinning::kFundingFillSameStepFundingCsv).string()) } },
            logger,
            1000.0,
            0,
            run_id);
        exchange.set_funding_apply_timing(timing);
        auto market_channel = exchange.get_market_channel();

        if (!exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy)) {
            ADD_FAILURE() << "failed to place first perp order";
            return 0.0;
        }
        if (!exchange.step()) {
            ADD_FAILURE() << "failed to step first bar";
            return 0.0;
        }
        if (!market_channel->Receive().has_value()) {
            ADD_FAILURE() << "missing first market payload";
            return 0.0;
        }

        if (!exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy)) {
            ADD_FAILURE() << "failed to place second perp order";
            return 0.0;
        }
        if (!exchange.step()) {
            ADD_FAILURE() << "failed to step second bar";
            return 0.0;
        }
        if (!market_channel->Receive().has_value()) {
            ADD_FAILURE() << "missing second market payload";
            return 0.0;
        }
        exchange.FillStatusSnapshot(snap);
        return snap.wallet_balance;
    };

    const double before_wallet = run_timing(
        BinanceExchange::FundingApplyTiming::BeforeMatching,
        QTrading::Infra::Tests::BaselineSemanticsPinning::kFundingFillSameStepBeforeMatching.run_id);
    const double after_wallet = run_timing(
        BinanceExchange::FundingApplyTiming::AfterMatching,
        QTrading::Infra::Tests::BaselineSemanticsPinning::kFundingFillSameStepAfterMatching.run_id);

    StopLogger();

    const auto funding_rows = FilterRowsByModule(QTrading::Log::LogModule::FundingEvent);
    std::vector<FundingEventDto> before_events;
    std::vector<FundingEventDto> after_events;
    for (const auto& row_view : funding_rows) {
        const auto* payload = RowPayloadCast<FundingEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->run_id == QTrading::Infra::Tests::BaselineSemanticsPinning::kFundingFillSameStepBeforeMatching.run_id) {
            before_events.push_back(*payload);
        }
        if (payload->run_id == QTrading::Infra::Tests::BaselineSemanticsPinning::kFundingFillSameStepAfterMatching.run_id) {
            after_events.push_back(*payload);
        }
    }

    ASSERT_EQ(before_events.size(), 1u);
    ASSERT_EQ(after_events.size(), 1u);
    EXPECT_EQ(before_events.front().funding_time, 60000u);
    EXPECT_EQ(after_events.front().funding_time, 60000u);
    EXPECT_NEAR(before_events.front().quantity, 1.0, 1e-12);
    EXPECT_NEAR(after_events.front().quantity, 2.0, 1e-12);
    EXPECT_NEAR(before_events.front().funding, -0.1, 1e-12);
    EXPECT_NEAR(after_events.front().funding, -0.2, 1e-12);
    EXPECT_NEAR(before_wallet - after_wallet, 0.1, 1e-6);
}

TEST_F(InfraLogTestFixture, LiquidationSyntheticFillContractRemainsVisible)
{
    using QTrading::Dto::Market::Binance::TradeKlineDto;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::AccountEventDto;
    using QTrading::Log::FileLogger::FeatherV2::PositionEventDto;

    SCOPED_TRACE(::testing::Message()
        << "baseline_pin id=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kLiquidationSyntheticFillContract.id
        << " run_id=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kLiquidationSyntheticFillContract.run_id
        << " baseline_input_version=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kBaselineInputVersion
        << " seed=" << QTrading::Infra::Tests::BaselineSemanticsPinning::kPinnedDeterministicSeed
        << " scenario=liquidation_synthetic_fill_contract_visibility");
    WriteBinanceCsv(
        tmp_dir / QTrading::Infra::Tests::BaselineSemanticsPinning::kLiquidationSyntheticFillTradeCsv,
        { { 0u, 1.0, 1.0, 1.0, 1.0, 10000.0, 30000u, 10000.0, 1, 0.0, 0.0 } });
    WriteBinanceCsv(
        tmp_dir / QTrading::Infra::Tests::BaselineSemanticsPinning::kLiquidationSyntheticFillMarkCsv,
        { { 0u, 1.0, 1.0, 1.0, 1.0, 10000.0, 30000u, 10000.0, 1, 0.0, 0.0 } });

    auto account = std::make_shared<Account>(350000.0, 0);
    account->set_symbol_leverage("BTCUSDT", 75.0);
    ASSERT_TRUE(account->place_order("BTCUSDT", 5000.0, 500.0, OrderSide::Buy, PositionSide::Both));
    TradeKlineDto open_kline{};
    open_kline.OpenPrice = 500.0;
    open_kline.HighPrice = 500.0;
    open_kline.LowPrice = 500.0;
    open_kline.ClosePrice = 500.0;
    open_kline.Volume = 10000.0;
    account->update_positions(
        std::unordered_map<std::string, TradeKlineDto>{ { "BTCUSDT", open_kline } },
        std::unordered_map<std::string, double>{ { "BTCUSDT", 500.0 } });

    {
        BinanceExchange exchange(
            { BinanceExchange::SymbolDataset{
                "BTCUSDT",
                (tmp_dir / QTrading::Infra::Tests::BaselineSemanticsPinning::kLiquidationSyntheticFillTradeCsv).string(),
                std::nullopt,
                (tmp_dir / QTrading::Infra::Tests::BaselineSemanticsPinning::kLiquidationSyntheticFillMarkCsv).string() } },
            logger,
            account,
            QTrading::Infra::Tests::BaselineSemanticsPinning::kLiquidationSyntheticFillContract.run_id);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        EXPECT_TRUE(exchange.get_all_positions().empty());
    }

    StopLogger();

    const auto account_rows = FilterRowsByModule(QTrading::Log::LogModule::AccountEvent);
    const auto position_rows = FilterRowsByModule(QTrading::Log::LogModule::PositionEvent);
    const auto order_event_rows = FilterRowsByModule(QTrading::Log::LogModule::OrderEvent);
    ASSERT_FALSE(account_rows.empty());
    ASSERT_FALSE(position_rows.empty());

    constexpr int64_t kSyntheticLiquidationOrderId = -999999;
    bool has_liq_order_event = false;
    bool has_liq_account_event = false;
    bool has_liq_position_event = false;
    for (const auto& row_view : order_event_rows) {
        const auto* payload = RowPayloadCast<QTrading::Log::FileLogger::FeatherV2::OrderEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->step_seq == 1u &&
            payload->symbol == "BTCUSDT" &&
            payload->order_id == kSyntheticLiquidationOrderId) {
            has_liq_order_event = true;
        }
    }
    for (const auto& row_view : account_rows) {
        const auto* payload = RowPayloadCast<AccountEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->step_seq == 1u &&
            payload->symbol == "BTCUSDT" &&
            payload->source_order_id == kSyntheticLiquidationOrderId) {
            has_liq_account_event = true;
        }
    }
    for (const auto& row_view : position_rows) {
        const auto* payload = RowPayloadCast<PositionEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->step_seq == 1u &&
            payload->symbol == "BTCUSDT" &&
            payload->source_order_id == kSyntheticLiquidationOrderId) {
            has_liq_position_event = true;
        }
    }

    // Prefer direct order_id assertion when OrderEvent exists; otherwise rely on
    // source_order_id contract exposed via AccountEvent/PositionEvent.
    EXPECT_TRUE(has_liq_order_event || (has_liq_account_event && has_liq_position_event));
    EXPECT_TRUE(has_liq_account_event);
    EXPECT_TRUE(has_liq_position_event);
}

TEST_F(InfraLogTestFixture, MarketEventAndFundingEventKeepExistingArrivalOrderWithinSingleStep)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::FundingEventDto;
    using QTrading::Log::FileLogger::FeatherV2::MarketEventDto;

    WriteBinanceCsv(
        tmp_dir / "event_order_market.csv",
        {
            { 0u, 100.0, 101.0, 99.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 101.0, 102.0, 100.0, 101.0, 1000.0, 90000u, 1000.0, 1, 0.0, 0.0 }
        });
    WriteFundingCsv(
        tmp_dir / "event_order_funding.csv",
        { { 60000u, 0.0001, 101.0 } });

    {
        BinanceExchange exchange(
            { BinanceExchange::SymbolDataset{
                "BTCUSDT",
                (tmp_dir / "event_order_market.csv").string(),
                (tmp_dir / "event_order_funding.csv").string() } },
            logger,
            1000.0,
            0,
            7100u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto market_rows = FilterRowsByModule(QTrading::Log::LogModule::MarketEvent);
    const auto funding_rows = FilterRowsByModule(QTrading::Log::LogModule::FundingEvent);
    ASSERT_EQ(funding_rows.size(), 1u);

    const ArrivedRowView* market_row = nullptr;
    for (const auto& row_view : market_rows) {
        const auto* payload = RowPayloadCast<MarketEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->step_seq == 2u) {
            market_row = &row_view;
            break;
        }
    }

    ASSERT_NE(market_row, nullptr);
    const auto* market = RowPayloadCast<MarketEventDto>(market_row->row);
    const auto* funding = RowPayloadCast<FundingEventDto>(funding_rows.front().row);
    ASSERT_NE(market, nullptr);
    ASSERT_NE(funding, nullptr);
    EXPECT_EQ(market->step_seq, 2u);
    EXPECT_EQ(funding->step_seq, 2u);
    EXPECT_LT(market_row->arrival_index, funding_rows.front().arrival_index);
    EXPECT_LT(market->event_seq, funding->event_seq);
}

TEST_F(InfraLogTestFixture, OrderPositionAndAccountEventKeepExistingArrivalOrderWithinSingleStep)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    WriteBinanceCsv(
        tmp_dir / "event_order_fill.csv",
        { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "event_order_fill.csv").string() } },
            logger,
            1000.0,
            0,
            7200u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto account_rows = FilterRowsByModule(QTrading::Log::LogModule::AccountEvent);
    const auto position_rows = FilterRowsByModule(QTrading::Log::LogModule::PositionEvent);
    const auto order_rows = FilterRowsByModule(QTrading::Log::LogModule::OrderEvent);
    ASSERT_FALSE(account_rows.empty());
    ASSERT_FALSE(position_rows.empty());
    ASSERT_FALSE(order_rows.empty());

    EXPECT_LT(account_rows.back().arrival_index, position_rows.front().arrival_index);
    EXPECT_LT(position_rows.back().arrival_index, order_rows.front().arrival_index);
}

TEST_F(InfraLogTestFixture, SnapshotLogAndEventLogKeepExistingArrivalOrderWithinSingleStep)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    WriteBinanceCsv(
        tmp_dir / "event_snapshot_vs_event.csv",
        { { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 } });

    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "event_snapshot_vs_event.csv").string() } },
            logger,
            1000.0,
            0,
            7300u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, 90.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    const auto account_snapshot_rows = FilterRowsByModule(QTrading::Log::LogModule::Account);
    const auto order_snapshot_rows = FilterRowsByModule(QTrading::Log::LogModule::Order);
    const auto market_event_rows = FilterRowsByModule(QTrading::Log::LogModule::MarketEvent);
    const auto account_event_rows = FilterRowsByModule(QTrading::Log::LogModule::AccountEvent);
    const auto order_event_rows = FilterRowsByModule(QTrading::Log::LogModule::OrderEvent);

    ASSERT_EQ(account_snapshot_rows.size(), 1u);
    ASSERT_EQ(order_snapshot_rows.size(), 1u);
    ASSERT_EQ(market_event_rows.size(), 1u);
    ASSERT_EQ(account_event_rows.size(), 1u);
    ASSERT_EQ(order_event_rows.size(), 1u);

    EXPECT_LT(account_snapshot_rows.front().arrival_index, order_snapshot_rows.front().arrival_index);
    EXPECT_LT(order_snapshot_rows.front().arrival_index, market_event_rows.front().arrival_index);
    EXPECT_LT(order_snapshot_rows.front().arrival_index, account_event_rows.front().arrival_index);
    EXPECT_LT(order_snapshot_rows.front().arrival_index, order_event_rows.front().arrival_index);
}

TEST_F(InfraLogTestFixture, LiquidationStepKeepsExistingEventArrivalOrder)
{
    using QTrading::Dto::Market::Binance::TradeKlineDto;
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Dto::Trading::PositionSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    WriteBinanceCsv(
        tmp_dir / "event_liquidation_trade.csv",
        { { 0u, 1.0, 1.0, 1.0, 1.0, 10000.0, 30000u, 10000.0, 1, 0.0, 0.0 } });
    WriteBinanceCsv(
        tmp_dir / "event_liquidation_mark.csv",
        { { 0u, 1.0, 1.0, 1.0, 1.0, 10000.0, 30000u, 10000.0, 1, 0.0, 0.0 } });

    size_t positions_after_liquidation = 0;
    auto account = std::make_shared<Account>(350000.0, 0);
    account->set_symbol_leverage("BTCUSDT", 75.0);
    ASSERT_TRUE(account->place_order("BTCUSDT", 5000.0, 500.0, OrderSide::Buy, PositionSide::Both));
    TradeKlineDto open_kline{};
    open_kline.OpenPrice = 500.0;
    open_kline.HighPrice = 500.0;
    open_kline.LowPrice = 500.0;
    open_kline.ClosePrice = 500.0;
    open_kline.Volume = 10000.0;
    account->update_positions(
        std::unordered_map<std::string, TradeKlineDto>{ { "BTCUSDT", open_kline } },
        std::unordered_map<std::string, double>{ { "BTCUSDT", 500.0 } });
    ASSERT_FALSE(account->get_all_positions().empty());

    {
        BinanceExchange exchange(
            { BinanceExchange::SymbolDataset{
                "BTCUSDT",
                (tmp_dir / "event_liquidation_trade.csv").string(),
                std::nullopt,
                (tmp_dir / "event_liquidation_mark.csv").string() } },
            logger,
            account,
            7400u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        positions_after_liquidation = exchange.get_all_positions().size();
    }

    StopLogger();

    const auto market_rows = FilterRowsByModule(QTrading::Log::LogModule::MarketEvent);
    const auto account_rows = FilterRowsByModule(QTrading::Log::LogModule::AccountEvent);
    const auto position_rows = FilterRowsByModule(QTrading::Log::LogModule::PositionEvent);
    const auto order_rows = FilterRowsByModule(QTrading::Log::LogModule::OrderEvent);

    EXPECT_EQ(positions_after_liquidation, 0u);
    ASSERT_FALSE(market_rows.empty());
    ASSERT_FALSE(account_rows.empty());
    ASSERT_FALSE(position_rows.empty());

    EXPECT_LT(market_rows.back().arrival_index, account_rows.front().arrival_index);
    EXPECT_LT(account_rows.back().arrival_index, position_rows.front().arrival_index);
    if (!order_rows.empty()) {
        EXPECT_LT(position_rows.back().arrival_index, order_rows.front().arrival_index);
    }
}

TEST_F(InfraLogTestFixture, FundingAndFillInSameStepKeepExistingEventArrivalOrder)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Log::FileLogger::FeatherV2::AccountEventDto;
    using QTrading::Log::FileLogger::FeatherV2::FundingEventDto;
    using QTrading::Log::FileLogger::FeatherV2::MarketEventDto;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventDto;
    using QTrading::Log::FileLogger::FeatherV2::PositionEventDto;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    WriteBinanceCsv(
        tmp_dir / "event_funding_fill_trade.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 101.0, 101.0, 101.0, 101.0, 1000.0, 90000u, 1010.0, 1, 0.0, 0.0 }
        });
    WriteFundingCsv(
        tmp_dir / "event_funding_fill_funding.csv",
        { { 60000u, 0.0001, 101.0 } });

    {
        BinanceExchange exchange(
            { BinanceExchange::SymbolDataset{
                "BTCUSDT",
                (tmp_dir / "event_funding_fill_trade.csv").string(),
                (tmp_dir / "event_funding_fill_funding.csv").string() } },
            logger,
            1000.0,
            0,
            7500u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 0.5, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
    }

    StopLogger();

    std::vector<ArrivedRowView> market_rows;
    std::vector<ArrivedRowView> funding_rows;
    std::vector<ArrivedRowView> account_rows;
    std::vector<ArrivedRowView> position_rows;
    std::vector<ArrivedRowView> order_rows;
    for (const auto& row_view : DrainAndSortRowsByArrival()) {
        if (row_view.row->module_id == ModuleId(QTrading::Log::LogModule::MarketEvent)) {
            const auto* payload = RowPayloadCast<MarketEventDto>(row_view.row);
            ASSERT_NE(payload, nullptr);
            if (payload->step_seq == 2u) {
                market_rows.push_back(row_view);
            }
        }
        else if (row_view.row->module_id == ModuleId(QTrading::Log::LogModule::FundingEvent)) {
            const auto* payload = RowPayloadCast<FundingEventDto>(row_view.row);
            ASSERT_NE(payload, nullptr);
            if (payload->step_seq == 2u) {
                funding_rows.push_back(row_view);
            }
        }
        else if (row_view.row->module_id == ModuleId(QTrading::Log::LogModule::AccountEvent)) {
            const auto* payload = RowPayloadCast<AccountEventDto>(row_view.row);
            ASSERT_NE(payload, nullptr);
            if (payload->step_seq == 2u) {
                account_rows.push_back(row_view);
            }
        }
        else if (row_view.row->module_id == ModuleId(QTrading::Log::LogModule::PositionEvent)) {
            const auto* payload = RowPayloadCast<PositionEventDto>(row_view.row);
            ASSERT_NE(payload, nullptr);
            if (payload->step_seq == 2u) {
                position_rows.push_back(row_view);
            }
        }
        else if (row_view.row->module_id == ModuleId(QTrading::Log::LogModule::OrderEvent)) {
            const auto* payload = RowPayloadCast<OrderEventDto>(row_view.row);
            ASSERT_NE(payload, nullptr);
            if (payload->step_seq == 2u) {
                order_rows.push_back(row_view);
            }
        }
    }

    ASSERT_EQ(market_rows.size(), 1u);
    ASSERT_EQ(funding_rows.size(), 1u);
    ASSERT_FALSE(account_rows.empty());
    ASSERT_FALSE(position_rows.empty());
    ASSERT_FALSE(order_rows.empty());

    EXPECT_LT(market_rows.back().arrival_index, funding_rows.front().arrival_index);
    EXPECT_LT(funding_rows.back().arrival_index, account_rows.front().arrival_index);
    EXPECT_LT(account_rows.back().arrival_index, position_rows.front().arrival_index);
    EXPECT_LT(position_rows.back().arrival_index, order_rows.front().arrival_index);
}

TEST_F(InfraLogTestFixture, PositionSnapshotReplayMatchesRuntimePositionsAtEachStep)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    struct RuntimePositionsAtTs {
        uint64_t ts{};
        std::vector<QTrading::dto::Position> positions;
    };

    WriteBinanceCsv(
        tmp_dir / "runtime_positions.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 110.0, 110.0, 110.0, 110.0, 1000.0, 90000u, 1100.0, 1, 0.0, 0.0 },
            { 120000u, 120.0, 120.0, 120.0, 120.0, 1000.0, 150000u, 1200.0, 1, 0.0, 0.0 }
        });

    std::vector<RuntimePositionsAtTs> runtime_positions;
    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "runtime_positions.csv").string() } },
            logger,
            1000.0,
            0,
            7600u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        auto step1 = market_channel->Receive();
        ASSERT_TRUE(step1.has_value());
        runtime_positions.push_back(RuntimePositionsAtTs{
            step1.value()->Timestamp,
            SortPositionsForComparison(exchange.get_all_positions())
        });

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 0.5, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        auto step2 = market_channel->Receive();
        ASSERT_TRUE(step2.has_value());
        runtime_positions.push_back(RuntimePositionsAtTs{
            step2.value()->Timestamp,
            SortPositionsForComparison(exchange.get_all_positions())
        });

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 0.25, OrderSide::Sell));
        ASSERT_TRUE(exchange.step());
        auto step3 = market_channel->Receive();
        ASSERT_TRUE(step3.has_value());
        runtime_positions.push_back(RuntimePositionsAtTs{
            step3.value()->Timestamp,
            SortPositionsForComparison(exchange.get_all_positions())
        });
    }

    StopLogger();

    ASSERT_EQ(runtime_positions.size(), 3u);
    const auto position_rows = FilterRowsByModule(QTrading::Log::LogModule::Position);
    ASSERT_EQ(position_rows.size(), runtime_positions.size());
    for (size_t i = 0; i < runtime_positions.size(); ++i) {
        const auto* payload = RowPayloadCast<QTrading::dto::Position>(position_rows[i].row);
        ASSERT_NE(payload, nullptr);
        const auto replayed_positions = SortPositionsForComparison(std::vector<QTrading::dto::Position>{ *payload });
        EXPECT_TRUE(PositionsEqualForComparison(runtime_positions[i].positions, replayed_positions));
    }
}

TEST_F(InfraLogTestFixture, OrderSnapshotReplayMatchesRuntimeOpenOrdersAtEachStep)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    struct RuntimeOrdersAtTs {
        uint64_t ts{};
        std::vector<QTrading::dto::Order> orders;
    };

    WriteBinanceCsv(
        tmp_dir / "runtime_orders.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 100.0, 100.0, 100.0, 100.0, 1000.0, 90000u, 1000.0, 1, 0.0, 0.0 },
            { 120000u, 100.0, 100.0, 100.0, 100.0, 1000.0, 150000u, 1000.0, 1, 0.0, 0.0 }
        });

    std::vector<RuntimeOrdersAtTs> runtime_orders;
    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "runtime_orders.csv").string() } },
            logger,
            1000.0,
            0,
            7700u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, 90.0, OrderSide::Buy, QTrading::Dto::Trading::PositionSide::Both, false, "cid-1"));
        ASSERT_TRUE(exchange.step());
        auto step1 = market_channel->Receive();
        ASSERT_TRUE(step1.has_value());
        runtime_orders.push_back(RuntimeOrdersAtTs{
            step1.value()->Timestamp,
            SortOrdersForComparison(exchange.get_all_open_orders())
        });

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 0.5, 110.0, OrderSide::Sell, QTrading::Dto::Trading::PositionSide::Both, false, "cid-2"));
        ASSERT_TRUE(exchange.step());
        auto step2 = market_channel->Receive();
        ASSERT_TRUE(step2.has_value());
        runtime_orders.push_back(RuntimeOrdersAtTs{
            step2.value()->Timestamp,
            SortOrdersForComparison(exchange.get_all_open_orders())
        });

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 0.25, 80.0, OrderSide::Buy, QTrading::Dto::Trading::PositionSide::Both, false, "cid-3"));
        ASSERT_TRUE(exchange.step());
        auto step3 = market_channel->Receive();
        ASSERT_TRUE(step3.has_value());
        runtime_orders.push_back(RuntimeOrdersAtTs{
            step3.value()->Timestamp,
            SortOrdersForComparison(exchange.get_all_open_orders())
        });
    }

    StopLogger();

    ASSERT_EQ(runtime_orders.size(), 3u);
    const auto order_rows = FilterRowsByModule(QTrading::Log::LogModule::Order);
    size_t next_row_index = 0;
    for (const auto& step_state : runtime_orders) {
        std::vector<QTrading::dto::Order> replayed_orders;
        replayed_orders.reserve(step_state.orders.size());
        for (size_t i = 0; i < step_state.orders.size(); ++i) {
            ASSERT_LT(next_row_index, order_rows.size());
            const auto* payload = RowPayloadCast<QTrading::dto::Order>(order_rows[next_row_index].row);
            ASSERT_NE(payload, nullptr);
            replayed_orders.push_back(*payload);
            ++next_row_index;
        }
        replayed_orders = SortOrdersForComparison(std::move(replayed_orders));
        EXPECT_TRUE(OrdersEqualForComparison(step_state.orders, replayed_orders));
    }
    EXPECT_EQ(next_row_index, order_rows.size());
}

TEST_F(InfraLogTestFixture, AccountSnapshotLogMatchesFillStatusSnapshotPerChangedStep)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;

    struct RuntimeAccountAtTs {
        uint64_t ts{};
        BinanceExchange::StatusSnapshot snapshot;
    };

    WriteBinanceCsv(
        tmp_dir / "runtime_account.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 110.0, 110.0, 110.0, 110.0, 1000.0, 90000u, 1100.0, 1, 0.0, 0.0 },
            { 120000u, 120.0, 120.0, 120.0, 120.0, 1000.0, 150000u, 1200.0, 1, 0.0, 0.0 }
        });

    std::vector<RuntimeAccountAtTs> runtime_accounts;
    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "runtime_account.csv").string() } },
            logger,
            1000.0,
            0,
            7800u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        auto step1 = market_channel->Receive();
        ASSERT_TRUE(step1.has_value());
        BinanceExchange::StatusSnapshot snap1{};
        exchange.FillStatusSnapshot(snap1);
        runtime_accounts.push_back(RuntimeAccountAtTs{ step1.value()->Timestamp, snap1 });

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 0.5, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        auto step2 = market_channel->Receive();
        ASSERT_TRUE(step2.has_value());
        BinanceExchange::StatusSnapshot snap2{};
        exchange.FillStatusSnapshot(snap2);
        runtime_accounts.push_back(RuntimeAccountAtTs{ step2.value()->Timestamp, snap2 });

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 0.25, OrderSide::Sell));
        ASSERT_TRUE(exchange.step());
        auto step3 = market_channel->Receive();
        ASSERT_TRUE(step3.has_value());
        BinanceExchange::StatusSnapshot snap3{};
        exchange.FillStatusSnapshot(snap3);
        runtime_accounts.push_back(RuntimeAccountAtTs{ step3.value()->Timestamp, snap3 });
    }

    StopLogger();

    const auto account_rows = FilterRowsByModule(QTrading::Log::LogModule::Account);
    ASSERT_EQ(account_rows.size(), runtime_accounts.size());
    for (size_t i = 0; i < runtime_accounts.size(); ++i) {
        const auto* payload = RowPayloadCast<QTrading::dto::AccountLog>(account_rows[i].row);
        ASSERT_NE(payload, nullptr);
        const auto& snap = runtime_accounts[i].snapshot;
        EXPECT_DOUBLE_EQ(payload->balance, snap.wallet_balance);
        EXPECT_DOUBLE_EQ(payload->unreal_pnl, snap.unrealized_pnl);
        EXPECT_DOUBLE_EQ(payload->equity, snap.margin_balance);
        EXPECT_DOUBLE_EQ(payload->perp_wallet_balance, snap.perp_wallet_balance);
        EXPECT_DOUBLE_EQ(payload->perp_available_balance, snap.perp_available_balance);
        EXPECT_DOUBLE_EQ(payload->perp_ledger_value, snap.perp_margin_balance);
        EXPECT_DOUBLE_EQ(payload->spot_cash_balance, snap.spot_cash_balance);
        EXPECT_DOUBLE_EQ(payload->spot_available_balance, snap.spot_available_balance);
        EXPECT_DOUBLE_EQ(payload->spot_inventory_value, snap.spot_inventory_value);
        EXPECT_DOUBLE_EQ(payload->spot_ledger_value, snap.spot_ledger_value);
        EXPECT_DOUBLE_EQ(payload->total_cash_balance, snap.total_cash_balance);
        EXPECT_DOUBLE_EQ(payload->total_ledger_value, snap.total_ledger_value);
    }
}

TEST_F(InfraLogTestFixture, AggregatedFillFeesMatchAccountWalletChange)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::AccountEventDto;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventDto;
    using QTrading::Log::FileLogger::FeatherV2::OrderEventType;

    WriteBinanceCsv(
        tmp_dir / "runtime_fill_fees.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 100.0, 100.0, 100.0, 100.0, 1000.0, 90000u, 1000.0, 1, 0.0, 0.0 }
        });

    BinanceExchange::StatusSnapshot initial_snap{};
    BinanceExchange::StatusSnapshot final_snap{};
    {
        BinanceExchange exchange(
            { { "BTCUSDT", (tmp_dir / "runtime_fill_fees.csv").string() } },
            logger,
            1000.0,
            0,
            7900u);
        auto market_channel = exchange.get_market_channel();

        exchange.FillStatusSnapshot(initial_snap);

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 0.5, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        exchange.FillStatusSnapshot(final_snap);
    }

    StopLogger();

    double filled_fee_sum = 0.0;
    for (const auto& row_view : FilterRowsByModule(QTrading::Log::LogModule::OrderEvent)) {
        const auto* payload = RowPayloadCast<OrderEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->event_type == static_cast<int32_t>(OrderEventType::Filled)) {
            filled_fee_sum += payload->fee_quote_equiv;
        }
    }

    double account_event_fee_sum = 0.0;
    for (const auto& row_view : FilterRowsByModule(QTrading::Log::LogModule::AccountEvent)) {
        const auto* payload = RowPayloadCast<AccountEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->request_id != 0u) {
            account_event_fee_sum += payload->fee_quote_equiv;
        }
    }

    EXPECT_GT(filled_fee_sum, 0.0);
    EXPECT_NEAR(initial_snap.wallet_balance - final_snap.wallet_balance, filled_fee_sum, 1e-9);
    EXPECT_NEAR(account_event_fee_sum, filled_fee_sum, 1e-12);
}

TEST_F(InfraLogTestFixture, AggregatedFundingEventsMatchAccountEventAndSnapshotWalletChange)
{
    using QTrading::Dto::Trading::OrderSide;
    using QTrading::Infra::Exchanges::BinanceSim::BinanceExchange;
    using QTrading::Log::FileLogger::FeatherV2::AccountEventDto;
    using QTrading::Log::FileLogger::FeatherV2::FundingEventDto;

    WriteBinanceCsv(
        tmp_dir / "runtime_funding_trade.csv",
        {
            { 0u, 100.0, 100.0, 100.0, 100.0, 1000.0, 30000u, 1000.0, 1, 0.0, 0.0 },
            { 60000u, 100.0, 100.0, 100.0, 100.0, 1000.0, 90000u, 1000.0, 1, 0.0, 0.0 },
            { 120000u, 100.0, 100.0, 100.0, 100.0, 1000.0, 150000u, 1000.0, 1, 0.0, 0.0 }
        });
    WriteFundingCsv(
        tmp_dir / "runtime_funding.csv",
        {
            { 60000u, 0.001, 100.0 },
            { 120000u, -0.002, 100.0 }
        });

    BinanceExchange::StatusSnapshot baseline_snap{};
    BinanceExchange::StatusSnapshot final_snap{};
    {
        BinanceExchange exchange(
            { BinanceExchange::SymbolDataset{
                "BTCUSDT",
                (tmp_dir / "runtime_funding_trade.csv").string(),
                (tmp_dir / "runtime_funding.csv").string() } },
            logger,
            1000.0,
            0,
            8000u);
        auto market_channel = exchange.get_market_channel();

        ASSERT_TRUE(exchange.perp.place_order("BTCUSDT", 1.0, OrderSide::Buy));
        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        exchange.FillStatusSnapshot(baseline_snap);

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());

        ASSERT_TRUE(exchange.step());
        ASSERT_TRUE(market_channel->Receive().has_value());
        exchange.FillStatusSnapshot(final_snap);
    }

    StopLogger();

    double funding_sum = 0.0;
    std::vector<uint64_t> funding_steps;
    for (const auto& row_view : FilterRowsByModule(QTrading::Log::LogModule::FundingEvent)) {
        const auto* payload = RowPayloadCast<FundingEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        funding_sum += payload->funding;
        funding_steps.push_back(payload->step_seq);
    }

    double funding_wallet_delta_sum = 0.0;
    for (const auto& row_view : FilterRowsByModule(QTrading::Log::LogModule::AccountEvent)) {
        const auto* payload = RowPayloadCast<AccountEventDto>(row_view.row);
        ASSERT_NE(payload, nullptr);
        if (payload->request_id != 0u) {
            continue;
        }
        if (std::find(funding_steps.begin(), funding_steps.end(), payload->step_seq) != funding_steps.end()) {
            funding_wallet_delta_sum += payload->wallet_delta;
        }
    }

    ASSERT_EQ(funding_steps.size(), 2u);
    EXPECT_NEAR(final_snap.wallet_balance - baseline_snap.wallet_balance, funding_sum, 1e-9);
    EXPECT_NEAR(funding_wallet_delta_sum, funding_sum, 1e-9);
}
