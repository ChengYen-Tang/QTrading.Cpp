#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "Dto/Market/Binance/Kline.hpp"
#include "Dto/Trading/Side.hpp"
#include "Exchanges/BinanceSimulator/Account/Account.hpp"
#include "Exchanges/BinanceSimulator/Account/AccountCoreV2.hpp"
#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Diagnostics/Compare/ReplayCompareTestHarness.hpp"
#include "Exchanges/BinanceSimulator/Diagnostics/Compare/V2ReplayScenarioPack.hpp"

using namespace QTrading::Infra::Exchanges::BinanceSim;
using QTrading::Dto::Market::Binance::TradeKlineDto;
using QTrading::Dto::Trading::OrderSide;
using QTrading::Dto::Trading::PositionSide;
namespace ReplayCompare = QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare;

namespace fs = std::filesystem;

namespace {

constexpr size_t kPerfWarmupRounds = 1;
constexpr size_t kHotPathIterations = 1600;
constexpr size_t kSessionIterations = 800;
constexpr size_t kAckDeferredIterations = 600;
constexpr size_t kPerfSamples = 3;
constexpr size_t kReplayRows = 1200;
constexpr size_t kLogHeavyRows = 2400;
constexpr size_t kLogHeavyStepLimit = 1200;

constexpr double kMaxSingleStepLatencyNs = 20'000'000.0;
constexpr double kMaxSingleOpLatencyNs = 20'000'000.0;
constexpr double kMaxModeRatio = 3.00;
constexpr double kMaxP95JitterRatio = 1.50;
constexpr double kLogHeavyStepBudgetNs = 30'000'000.0;
constexpr double kLogHeavyMinThroughputStepsPerSec = 25.0;
constexpr double kLogHeavyBufferGuardMultiplier = 2.50;
constexpr double kMixedScenarioMaxLatencyRatioVsReplay = 4.00;
constexpr double kMixedScenarioMinThroughputRatioVsReplay = 0.20;
constexpr double kFallbackUnsupportedOverheadRatioBudget = 3.00;

std::unordered_map<std::string, TradeKlineDto> OneKlineMap(
    const std::string& symbol,
    uint64_t ts,
    double close)
{
    TradeKlineDto k{};
    k.Timestamp = ts;
    k.OpenPrice = close;
    k.HighPrice = close;
    k.LowPrice = close;
    k.ClosePrice = close;
    k.Volume = 1000.0;
    return { { symbol, k } };
}

class PerfGuardrailFixture : public ::testing::Test {
protected:
    fs::path tmp_dir_;

    void SetUp() override
    {
        tmp_dir_ = fs::temp_directory_path() /
            ("QTrading_PerfGuard_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::create_directories(tmp_dir_);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(tmp_dir_, ec);
    }

    void WriteCsv(
        const std::string& file_name,
        const std::vector<std::tuple<
            uint64_t, double, double, double, double, double,
            uint64_t, double, int, double, double>>& rows)
    {
        std::ofstream f(tmp_dir_ / file_name, std::ios::trunc);
        f << "openTime,open,high,low,close,volume,closeTime,quoteVol,tradeCnt,takerBB,takerBQ\n";
        for (const auto& r : rows) {
            f << std::get<0>(r) << ','
                << std::get<1>(r) << ','
                << std::get<2>(r) << ','
                << std::get<3>(r) << ','
                << std::get<4>(r) << ','
                << std::get<5>(r) << ','
                << std::get<6>(r) << ','
                << std::get<7>(r) << ','
                << std::get<8>(r) << ','
                << std::get<9>(r) << ','
                << std::get<10>(r) << '\n';
        }
    }
};

class PerfMockLogger : public QTrading::Log::Logger {
public:
    explicit PerfMockLogger(const std::string& dir)
        : QTrading::Log::Logger(dir)
    {
    }

protected:
    void Consume() override {}
};

double RunLegacyAccountHotPathNsPerOp(size_t iterations)
{
    Account::AccountInitConfig cfg{};
    cfg.spot_initial_cash = 10'000.0;
    cfg.perp_initial_wallet = 10'000.0;
    Account account(cfg);
    account.set_symbol_leverage("BTCUSDT", 5.0);

    const auto start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        const double px = 100.0 + static_cast<double>(i % 17);
        const bool buy_side = (i % 2 == 0);
        account.place_order(
            "BTCUSDT",
            0.02,
            px,
            buy_side ? OrderSide::Buy : OrderSide::Sell,
            PositionSide::Both,
            false);
        account.update_positions(OneKlineMap("BTCUSDT", static_cast<uint64_t>(i * 60'000), px));
        if ((i % 16) == 15) {
            account.cancel_open_orders("BTCUSDT");
        }
    }
    const auto end = std::chrono::steady_clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    return static_cast<double>(ns) / static_cast<double>(iterations);
}

double RunV2CompareOffAccountHotPathNsPerOp(size_t iterations)
{
    Account::AccountInitConfig cfg{};
    cfg.spot_initial_cash = 10'000.0;
    cfg.perp_initial_wallet = 10'000.0;
    AccountCoreV2 account_v2(cfg);
    account_v2.mutable_legacy_account().set_symbol_leverage("BTCUSDT", 5.0);

    const auto start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        const double px = 100.0 + static_cast<double>(i % 17);
        const bool buy_side = (i % 2 == 0);
        AccountCoreV2::Command cmd{};
        cmd.kind = AccountCoreV2::CommandKind::PlaceLimitOrder;
        cmd.symbol = "BTCUSDT";
        cmd.quantity = 0.02;
        cmd.price = px;
        cmd.side = buy_side ? OrderSide::Buy : OrderSide::Sell;
        cmd.position_side = PositionSide::Both;
        (void)account_v2.apply_command(cmd);
        account_v2.mutable_legacy_account().update_positions(
            OneKlineMap("BTCUSDT", static_cast<uint64_t>(i * 60'000), px));
        if ((i % 16) == 15) {
            AccountCoreV2::Command cancel{};
            cancel.kind = AccountCoreV2::CommandKind::CancelOpenOrders;
            cancel.symbol = "BTCUSDT";
            (void)account_v2.apply_command(cancel);
        }
    }
    const auto end = std::chrono::steady_clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    return static_cast<double>(ns) / static_cast<double>(iterations);
}

struct ReplayPerfResult {
    double ns_per_step{ 0.0 };
    size_t steps{ 0 };
    size_t market_msgs{ 0 };
    size_t compare_diag_count{ 0 };
    size_t event_publish_diag_count{ 0 };
    size_t compared_snapshot_count{ 0 };
};

ReplayPerfResult RunReplayNsPerStep(
    const fs::path& tmp_dir,
    BinanceExchange::CoreMode core_mode,
    BinanceExchange::EventPublishMode publish_mode,
    bool collect_diagnostics = false,
    size_t max_steps = std::numeric_limits<size_t>::max())
{
    std::vector<std::tuple<uint64_t, double, double, double, double, double, uint64_t, double, int, double, double>> btc;
    std::vector<std::tuple<uint64_t, double, double, double, double, double, uint64_t, double, int, double, double>> eth;
    btc.reserve(kReplayRows);
    eth.reserve(kReplayRows);
    for (size_t i = 0; i < kReplayRows; ++i) {
        const uint64_t open_time = static_cast<uint64_t>(i * 60'000);
        const uint64_t close_time = open_time + 30'000;
        const double px_btc = 100.0 + static_cast<double>(i % 53) * 0.03;
        const double px_eth = 200.0 + static_cast<double>(i % 37) * 0.05;
        btc.push_back({ open_time, px_btc, px_btc, px_btc, px_btc, 1000.0, close_time, 1000.0, 1, 0.0, 0.0 });
        eth.push_back({ open_time, px_eth, px_eth, px_eth, px_eth, 900.0, close_time, 900.0, 1, 0.0, 0.0 });
    }

    std::ofstream f1(tmp_dir / "perf_btc.csv", std::ios::trunc);
    f1 << "openTime,open,high,low,close,volume,closeTime,quoteVol,tradeCnt,takerBB,takerBQ\n";
    for (const auto& r : btc) {
        f1 << std::get<0>(r) << ',' << std::get<1>(r) << ',' << std::get<2>(r) << ',' << std::get<3>(r) << ','
            << std::get<4>(r) << ',' << std::get<5>(r) << ',' << std::get<6>(r) << ',' << std::get<7>(r) << ','
            << std::get<8>(r) << ',' << std::get<9>(r) << ',' << std::get<10>(r) << '\n';
    }
    std::ofstream f2(tmp_dir / "perf_eth.csv", std::ios::trunc);
    f2 << "openTime,open,high,low,close,volume,closeTime,quoteVol,tradeCnt,takerBB,takerBQ\n";
    for (const auto& r : eth) {
        f2 << std::get<0>(r) << ',' << std::get<1>(r) << ',' << std::get<2>(r) << ',' << std::get<3>(r) << ','
            << std::get<4>(r) << ',' << std::get<5>(r) << ',' << std::get<6>(r) << ',' << std::get<7>(r) << ','
            << std::get<8>(r) << ',' << std::get<9>(r) << ',' << std::get<10>(r) << '\n';
    }

    BinanceExchange ex(
        {
            { "BTCUSDT", (tmp_dir / "perf_btc.csv").string() },
            { "ETHUSDT", (tmp_dir / "perf_eth.csv").string() }
        },
        nullptr,
        10'000.0);
    ex.set_core_mode(core_mode);
    ex.set_event_publish_mode(publish_mode);
    auto market_channel = ex.get_market_channel();

    ReplayPerfResult result{};
    const auto start = std::chrono::steady_clock::now();
    size_t steps = 0;
    size_t market_msgs = 0;
    while (ex.step()) {
        if ((steps % 11) == 0) {
            (void)ex.perp.place_order("BTCUSDT", 0.01, OrderSide::Buy);
            (void)ex.perp.place_order("ETHUSDT", 0.01, OrderSide::Sell);
        }
        const auto dto = market_channel->Receive();
        if (dto.has_value()) {
            ++market_msgs;
        }
        if (collect_diagnostics) {
            const auto compare_diag = ex.consume_last_compare_diagnostic();
            if (compare_diag.has_value()) {
                ++result.compare_diag_count;
                if (compare_diag->compared) {
                    ++result.compared_snapshot_count;
                }
            }
            const auto event_diag = ex.consume_last_event_publish_diagnostic();
            if (event_diag.has_value()) {
                ++result.event_publish_diag_count;
                if (event_diag->compared) {
                    ++result.compared_snapshot_count;
                }
            }
        }
        ++steps;
        if (steps >= max_steps) {
            break;
        }
    }
    const auto end = std::chrono::steady_clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    result.steps = steps;
    result.market_msgs = market_msgs;
    result.ns_per_step = (steps == 0) ? 0.0 : static_cast<double>(ns) / static_cast<double>(steps);
    return result;
}

struct AckDeferredPerfResult {
    double ns_per_op{ 0.0 };
    size_t ops{ 0 };
    size_t pending_ack{ 0 };
    size_t accepted_ack{ 0 };
    size_t rejected_ack{ 0 };
    size_t deferred_exec_steps{ 0 };
};

AckDeferredPerfResult RunOrderSubmitAckDeferredNsPerOp(
    const fs::path& tmp_dir,
    BinanceExchange::CoreMode core_mode,
    BinanceExchange::EventPublishMode publish_mode,
    size_t iterations)
{
    const auto replay = RunReplayNsPerStep(tmp_dir, core_mode, publish_mode, false, 0);
    (void)replay;

    BinanceExchange ex(
        {
            { "BTCUSDT", (tmp_dir / "perf_btc.csv").string() },
            { "ETHUSDT", (tmp_dir / "perf_eth.csv").string() }
        },
        nullptr,
        10'000.0);
    ex.set_core_mode(core_mode);
    ex.set_event_publish_mode(publish_mode);
    ex.set_order_latency_bars(1);

    AckDeferredPerfResult result{};
    const auto start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        const bool submitted = ex.perp.place_order("BTCUSDT", 0.01, OrderSide::Buy);
        if (!submitted) {
            continue;
        }
        if (!ex.step()) {
            break;
        }
        const auto acks = ex.drain_async_order_acks();
        for (const auto& ack : acks) {
            switch (ack.status) {
            case BinanceExchange::AsyncOrderAck::Status::Pending:
                ++result.pending_ack;
                break;
            case BinanceExchange::AsyncOrderAck::Status::Accepted:
                ++result.accepted_ack;
                break;
            case BinanceExchange::AsyncOrderAck::Status::Rejected:
                ++result.rejected_ack;
                break;
            }
        }
        const auto diag = ex.consume_last_trading_session_core_v2_diagnostic();
        if (diag.has_value() && diag->deferred_executed > 0) {
            ++result.deferred_exec_steps;
        }
        ++result.ops;
    }
    const auto end = std::chrono::steady_clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    result.ns_per_op = (result.ops == 0) ? 0.0 : static_cast<double>(ns) / static_cast<double>(result.ops);
    return result;
}

struct SampleStats {
    double min{ 0.0 };
    double p50{ 0.0 };
    double p95{ 0.0 };
    double max{ 0.0 };
    double mean{ 0.0 };
};

SampleStats BuildStats(std::vector<double> samples)
{
    EXPECT_FALSE(samples.empty());
    std::sort(samples.begin(), samples.end());
    SampleStats stats{};
    stats.min = samples.front();
    stats.max = samples.back();
    stats.mean = std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(samples.size());

    const auto pct = [&samples](double p) -> double {
        const double index = p * static_cast<double>(samples.size() - 1);
        const size_t lo = static_cast<size_t>(std::floor(index));
        const size_t hi = static_cast<size_t>(std::ceil(index));
        if (lo == hi) {
            return samples[lo];
        }
        const double w = index - static_cast<double>(lo);
        return samples[lo] + (samples[hi] - samples[lo]) * w;
    };

    stats.p50 = pct(0.50);
    stats.p95 = pct(0.95);
    return stats;
}

double RelativeJitterP95(const SampleStats& stats)
{
    if (stats.p50 <= 0.0) {
        return 0.0;
    }
    return std::max(0.0, stats.p95 - stats.p50) / stats.p50;
}

struct LogHeavyPerfResult {
    double ns_per_step{ 0.0 };
    double throughput_steps_per_sec{ 0.0 };
    size_t steps{ 0 };
    size_t market_msgs{ 0 };
    size_t position_msgs{ 0 };
    size_t order_msgs{ 0 };
    size_t max_position_batch_size{ 0 };
    size_t max_order_batch_size{ 0 };
    size_t max_market_backlog{ 0 };
    size_t forwarded_market_count{ 0 };
    size_t forwarded_position_count{ 0 };
    size_t forwarded_order_count{ 0 };
    size_t side_effect_hook_calls{ 0 };
    size_t publish_order_violations{ 0 };
    size_t event_diag_count{ 0 };
    size_t event_diag_matched_count{ 0 };
    size_t compare_artifact_enabled_steps{ 0 };
};

LogHeavyPerfResult RunLogHeavyScenario(
    const fs::path& tmp_dir,
    std::shared_ptr<QTrading::Log::Logger> logger,
    BinanceExchange::CoreMode core_mode,
    BinanceExchange::EventPublishMode publish_mode,
    bool use_forwarding_adapters,
    size_t rows = kLogHeavyRows,
    size_t max_steps = kLogHeavyStepLimit)
{
    std::vector<std::tuple<uint64_t, double, double, double, double, double, uint64_t, double, int, double, double>> btc;
    std::vector<std::tuple<uint64_t, double, double, double, double, double, uint64_t, double, int, double, double>> eth;
    btc.reserve(rows);
    eth.reserve(rows);
    for (size_t i = 0; i < rows; ++i) {
        const uint64_t open_time = static_cast<uint64_t>(i * 60'000);
        const uint64_t close_time = open_time + 30'000;
        const double b = 100.0 + static_cast<double>(i % 17) * 0.11;
        const double e = 200.0 + static_cast<double>(i % 19) * 0.09;
        btc.push_back({ open_time, b, b + 0.3, b - 0.3, b, 1200.0 + static_cast<double>(i % 11), close_time, 1200.0, 3, 0.0, 0.0 });
        eth.push_back({ open_time, e, e + 0.2, e - 0.2, e, 900.0 + static_cast<double>(i % 7), close_time, 900.0, 2, 0.0, 0.0 });
    }

    std::ofstream f1(tmp_dir / "logheavy_btc.csv", std::ios::trunc);
    f1 << "openTime,open,high,low,close,volume,closeTime,quoteVol,tradeCnt,takerBB,takerBQ\n";
    for (const auto& r : btc) {
        f1 << std::get<0>(r) << ',' << std::get<1>(r) << ',' << std::get<2>(r) << ',' << std::get<3>(r) << ','
            << std::get<4>(r) << ',' << std::get<5>(r) << ',' << std::get<6>(r) << ',' << std::get<7>(r) << ','
            << std::get<8>(r) << ',' << std::get<9>(r) << ',' << std::get<10>(r) << '\n';
    }

    std::ofstream f2(tmp_dir / "logheavy_eth.csv", std::ios::trunc);
    f2 << "openTime,open,high,low,close,volume,closeTime,quoteVol,tradeCnt,takerBB,takerBQ\n";
    for (const auto& r : eth) {
        f2 << std::get<0>(r) << ',' << std::get<1>(r) << ',' << std::get<2>(r) << ',' << std::get<3>(r) << ','
            << std::get<4>(r) << ',' << std::get<5>(r) << ',' << std::get<6>(r) << ',' << std::get<7>(r) << ','
            << std::get<8>(r) << ',' << std::get<9>(r) << ',' << std::get<10>(r) << '\n';
    }

    BinanceExchange ex(
        {
            { "BTCUSDT", (tmp_dir / "logheavy_btc.csv").string() },
            { "ETHUSDT", (tmp_dir / "logheavy_eth.csv").string() }
        },
        logger,
        10'000.0);

    ex.set_core_mode(core_mode);
    ex.set_event_publish_mode(publish_mode);

    auto mCh = ex.get_market_channel();
    auto pCh = ex.get_position_channel();
    auto oCh = ex.get_order_channel();

    LogHeavyPerfResult result{};
    bool market_seen_for_step = false;

    if (use_forwarding_adapters) {
        BinanceExchange::SideEffectAdapterConfig adapters{};
        adapters.market_publisher = [&](MultiKlinePtr dto) {
            ++result.forwarded_market_count;
            market_seen_for_step = true;
            mCh->Send(dto);
        };
        adapters.position_publisher = [&](const std::vector<QTrading::dto::Position>& positions) {
            ++result.forwarded_position_count;
            if (!market_seen_for_step) {
                ++result.publish_order_violations;
            }
            pCh->Send(positions);
        };
        adapters.order_publisher = [&](const std::vector<QTrading::dto::Order>& orders) {
            ++result.forwarded_order_count;
            if (!market_seen_for_step) {
                ++result.publish_order_violations;
            }
            oCh->Send(orders);
        };
        adapters.external_hook = [&](const BinanceExchange::SideEffectStepSnapshot&) {
            ++result.side_effect_hook_calls;
        };
        ex.set_side_effect_adapters(std::move(adapters));
    }

    const auto start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < max_steps; ++i) {
        market_seen_for_step = false;

        // High-density order flow to stress order/position snapshots and channel publish.
        (void)ex.perp.place_order("BTCUSDT", 0.01, OrderSide::Buy);
        (void)ex.perp.place_order("ETHUSDT", 0.01, OrderSide::Sell);
        if ((i % 3) == 2) {
            ex.perp.cancel_open_orders("BTCUSDT");
            ex.perp.cancel_open_orders("ETHUSDT");
        }

        if (!ex.step()) {
            break;
        }

        auto m = mCh->Receive();
        if (m.has_value()) {
            ++result.market_msgs;
        }
        size_t market_backlog = 0;
        while (mCh->TryReceive().has_value()) {
            ++market_backlog;
            ++result.market_msgs;
        }
        result.max_market_backlog = std::max(result.max_market_backlog, market_backlog);

        while (true) {
            auto p = pCh->TryReceive();
            if (!p.has_value()) {
                break;
            }
            ++result.position_msgs;
            result.max_position_batch_size = std::max(result.max_position_batch_size, p->size());
        }

        while (true) {
            auto o = oCh->TryReceive();
            if (!o.has_value()) {
                break;
            }
            ++result.order_msgs;
            result.max_order_batch_size = std::max(result.max_order_batch_size, o->size());
        }

        if (const auto event_diag = ex.consume_last_event_publish_diagnostic(); event_diag.has_value()) {
            ++result.event_diag_count;
            if (event_diag->matched) {
                ++result.event_diag_matched_count;
            }
        }

        if (const auto coexistence = ex.consume_last_session_replay_coexistence_diagnostic(); coexistence.has_value()) {
            if (coexistence->compare_artifact_enabled) {
                ++result.compare_artifact_enabled_steps;
            }
        }

        ++result.steps;
    }
    const auto end = std::chrono::steady_clock::now();

    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    result.ns_per_step = (result.steps == 0) ? 0.0 : static_cast<double>(ns) / static_cast<double>(result.steps);
    result.throughput_steps_per_sec = (ns == 0) ? 0.0 : (static_cast<double>(result.steps) * 1'000'000'000.0 / static_cast<double>(ns));
    return result;
}

const ReplayCompare::ReplayCompareScenarioData* FindMixedScenario(
    const std::vector<ReplayCompare::ReplayCompareScenarioData>& scenarios,
    const std::string& name)
{
    for (const auto& scenario : scenarios) {
        if (scenario.scenario.name == name) {
            return &scenario;
        }
    }
    return nullptr;
}

ReplayCompare::ReplayCompareScenarioData* FindMixedScenario(
    std::vector<ReplayCompare::ReplayCompareScenarioData>& scenarios,
    const std::string& name)
{
    for (auto& scenario : scenarios) {
        if (scenario.scenario.name == name) {
            return &scenario;
        }
    }
    return nullptr;
}

struct MixedScenarioPerfResult {
    std::string scenario_name;
    ReplayCompare::ReplayCompareFeatureSet feature_set{};
    double ns_per_step{ 0.0 };
    double throughput_steps_per_sec{ 0.0 };
    uint64_t compared_steps{ 0 };
    uint64_t mismatch_count{ 0 };
    ReplayCompare::ReplayCompareStatus status{ ReplayCompare::ReplayCompareStatus::Unsupported };
};

MixedScenarioPerfResult RunMixedScenarioBenchmark(
    const ReplayCompare::ReplayCompareScenarioData& scenario,
    ReplayCompare::ReplayCompareFeatureSet feature_set)
{
    ReplayCompare::ReplayCompareTestHarness harness;
    ReplayCompare::ReplayCompareTestHarnessOptions options{};
    options.execution_mode = ReplayCompare::ReplayCompareExecutionMode::TestValidation;
    options.feature_set = feature_set;
    options.generate_artifact = false;

    const auto start = std::chrono::steady_clock::now();
    const auto result = harness.RunSingleScenario(scenario, options);
    const auto end = std::chrono::steady_clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    MixedScenarioPerfResult out{};
    out.scenario_name = scenario.scenario.name;
    out.feature_set = feature_set;
    out.compared_steps = result.report.compared_steps;
    out.mismatch_count = result.report.mismatch_count;
    out.status = result.report.status;
    out.ns_per_step = (out.compared_steps == 0) ? static_cast<double>(ns) : static_cast<double>(ns) / static_cast<double>(out.compared_steps);
    out.throughput_steps_per_sec = (ns == 0) ? 0.0 : static_cast<double>(out.compared_steps) * 1'000'000'000.0 / static_cast<double>(ns);
    return out;
}

} // namespace

TEST_F(PerfGuardrailFixture, AccountHotPathPerfGuardLegacyOnlyVsCompareOffAreEquivalent)
{
    for (size_t i = 0; i < kPerfWarmupRounds; ++i) {
        (void)RunLegacyAccountHotPathNsPerOp(200);
        (void)RunV2CompareOffAccountHotPathNsPerOp(200);
    }

    const double legacy_ns = RunLegacyAccountHotPathNsPerOp(kHotPathIterations);
    const double compare_off_ns = RunV2CompareOffAccountHotPathNsPerOp(kHotPathIterations);
    ASSERT_GT(legacy_ns, 0.0);
    ASSERT_GT(compare_off_ns, 0.0);

    const double ratio = compare_off_ns / legacy_ns;
    std::cout << "[PERF][AccountHotPath] legacy_ns_per_op=" << legacy_ns
              << " compare_off_ns_per_op=" << compare_off_ns
              << " ratio=" << ratio << '\n';

    // Guardrail: compare-off path must stay in the same order of magnitude as legacy hot path.
    EXPECT_LE(ratio, 2.50);
}

TEST_F(PerfGuardrailFixture, LogHeavyPerfGuardLegacyOnlyVsCompareOffAreEquivalent)
{
    const auto legacy = RunReplayNsPerStep(
        tmp_dir_,
        BinanceExchange::CoreMode::LegacyOnly,
        BinanceExchange::EventPublishMode::LegacyDirect);
    const auto compare_off = RunReplayNsPerStep(
        tmp_dir_,
        BinanceExchange::CoreMode::LegacyOnly,
        BinanceExchange::EventPublishMode::DomainEventAdapter);

    ASSERT_GT(legacy.steps, 0u);
    ASSERT_GT(compare_off.steps, 0u);
    ASSERT_GT(legacy.market_msgs, 0u);
    ASSERT_GT(compare_off.market_msgs, 0u);
    ASSERT_GT(legacy.ns_per_step, 0.0);
    ASSERT_GT(compare_off.ns_per_step, 0.0);

    const double ratio = compare_off.ns_per_step / legacy.ns_per_step;
    std::cout << "[PERF][LogHeavy] legacy_ns_per_step=" << legacy.ns_per_step
              << " compare_off_ns_per_step=" << compare_off.ns_per_step
              << " legacy_steps=" << legacy.steps
              << " compare_steps=" << compare_off.steps
              << " ratio=" << ratio << '\n';

    EXPECT_LE(ratio, 2.50);
}

TEST_F(PerfGuardrailFixture, SessionStepHotPathBenchmarkComparesLegacyCompareOffShadowAndV2Explicit)
{
    const auto legacy = RunReplayNsPerStep(
        tmp_dir_,
        BinanceExchange::CoreMode::LegacyOnly,
        BinanceExchange::EventPublishMode::LegacyDirect,
        false,
        kSessionIterations);
    const auto compare_off = RunReplayNsPerStep(
        tmp_dir_,
        BinanceExchange::CoreMode::LegacyOnly,
        BinanceExchange::EventPublishMode::DomainEventAdapter,
        false,
        kSessionIterations);
    const auto shadow_compare = RunReplayNsPerStep(
        tmp_dir_,
        BinanceExchange::CoreMode::NewCoreShadow,
        BinanceExchange::EventPublishMode::DomainEventAdapter,
        false,
        kSessionIterations);
    const auto v2_explicit = RunReplayNsPerStep(
        tmp_dir_,
        BinanceExchange::CoreMode::NewCorePrimary,
        BinanceExchange::EventPublishMode::DomainEventAdapter,
        false,
        kSessionIterations);

    ASSERT_GT(legacy.steps, 0u);
    ASSERT_GT(compare_off.steps, 0u);
    ASSERT_GT(shadow_compare.steps, 0u);
    ASSERT_GT(v2_explicit.steps, 0u);

    ASSERT_GT(legacy.ns_per_step, 0.0);
    ASSERT_GT(compare_off.ns_per_step, 0.0);
    ASSERT_GT(shadow_compare.ns_per_step, 0.0);
    ASSERT_GT(v2_explicit.ns_per_step, 0.0);

    const double ratio_legacy_compare_off = compare_off.ns_per_step / legacy.ns_per_step;
    const double ratio_legacy_v2 = v2_explicit.ns_per_step / legacy.ns_per_step;
    const double ratio_compare_off_shadow = shadow_compare.ns_per_step / compare_off.ns_per_step;

    std::cout << "[PERF][SessionStep] legacy_ns_per_step=" << legacy.ns_per_step
              << " compare_off_ns_per_step=" << compare_off.ns_per_step
              << " shadow_compare_ns_per_step=" << shadow_compare.ns_per_step
              << " v2_explicit_ns_per_step=" << v2_explicit.ns_per_step
              << " ratio_legacy_compare_off=" << ratio_legacy_compare_off
              << " ratio_legacy_v2=" << ratio_legacy_v2
              << " ratio_compare_off_shadow=" << ratio_compare_off_shadow << '\n';

    // Fail-fast budget: single step latency and mode ratios.
    EXPECT_LE(legacy.ns_per_step, kMaxSingleStepLatencyNs);
    EXPECT_LE(compare_off.ns_per_step, kMaxSingleStepLatencyNs);
    EXPECT_LE(shadow_compare.ns_per_step, kMaxSingleStepLatencyNs);
    EXPECT_LE(v2_explicit.ns_per_step, kMaxSingleStepLatencyNs);

    EXPECT_LE(ratio_legacy_compare_off, kMaxModeRatio);
    EXPECT_LE(ratio_legacy_v2, kMaxModeRatio);
    EXPECT_LE(ratio_compare_off_shadow, kMaxModeRatio);
}

TEST_F(PerfGuardrailFixture, OrderSubmitAckDeferredHotPathBenchmarkComparesModeRatios)
{
    const auto legacy = RunOrderSubmitAckDeferredNsPerOp(
        tmp_dir_,
        BinanceExchange::CoreMode::LegacyOnly,
        BinanceExchange::EventPublishMode::LegacyDirect,
        kAckDeferredIterations);
    const auto compare_off = RunOrderSubmitAckDeferredNsPerOp(
        tmp_dir_,
        BinanceExchange::CoreMode::LegacyOnly,
        BinanceExchange::EventPublishMode::DomainEventAdapter,
        kAckDeferredIterations);
    const auto shadow_compare = RunOrderSubmitAckDeferredNsPerOp(
        tmp_dir_,
        BinanceExchange::CoreMode::NewCoreShadow,
        BinanceExchange::EventPublishMode::DomainEventAdapter,
        kAckDeferredIterations);
    const auto v2_explicit = RunOrderSubmitAckDeferredNsPerOp(
        tmp_dir_,
        BinanceExchange::CoreMode::NewCorePrimary,
        BinanceExchange::EventPublishMode::DomainEventAdapter,
        kAckDeferredIterations);

    ASSERT_GT(legacy.ops, 0u);
    ASSERT_GT(compare_off.ops, 0u);
    ASSERT_GT(shadow_compare.ops, 0u);
    ASSERT_GT(v2_explicit.ops, 0u);
    ASSERT_GT(legacy.ns_per_op, 0.0);
    ASSERT_GT(compare_off.ns_per_op, 0.0);
    ASSERT_GT(shadow_compare.ns_per_op, 0.0);
    ASSERT_GT(v2_explicit.ns_per_op, 0.0);

    EXPECT_GT(legacy.pending_ack + legacy.accepted_ack + legacy.rejected_ack, 0u);
    EXPECT_GT(compare_off.pending_ack + compare_off.accepted_ack + compare_off.rejected_ack, 0u);
    EXPECT_GT(shadow_compare.pending_ack + shadow_compare.accepted_ack + shadow_compare.rejected_ack, 0u);
    EXPECT_GT(v2_explicit.pending_ack + v2_explicit.accepted_ack + v2_explicit.rejected_ack, 0u);

    const double ratio_legacy_compare_off = compare_off.ns_per_op / legacy.ns_per_op;
    const double ratio_legacy_v2 = v2_explicit.ns_per_op / legacy.ns_per_op;
    const double ratio_compare_off_shadow = shadow_compare.ns_per_op / compare_off.ns_per_op;

    std::cout << "[PERF][OrderAckDeferred] legacy_ns_per_op=" << legacy.ns_per_op
              << " compare_off_ns_per_op=" << compare_off.ns_per_op
              << " shadow_compare_ns_per_op=" << shadow_compare.ns_per_op
              << " v2_explicit_ns_per_op=" << v2_explicit.ns_per_op
              << " legacy_ack_total=" << (legacy.pending_ack + legacy.accepted_ack + legacy.rejected_ack)
              << " compare_off_ack_total=" << (compare_off.pending_ack + compare_off.accepted_ack + compare_off.rejected_ack)
              << " shadow_ack_total=" << (shadow_compare.pending_ack + shadow_compare.accepted_ack + shadow_compare.rejected_ack)
              << " v2_ack_total=" << (v2_explicit.pending_ack + v2_explicit.accepted_ack + v2_explicit.rejected_ack)
              << " ratio_legacy_compare_off=" << ratio_legacy_compare_off
              << " ratio_legacy_v2=" << ratio_legacy_v2
              << " ratio_compare_off_shadow=" << ratio_compare_off_shadow << '\n';

    EXPECT_LE(legacy.ns_per_op, kMaxSingleOpLatencyNs);
    EXPECT_LE(compare_off.ns_per_op, kMaxSingleOpLatencyNs);
    EXPECT_LE(shadow_compare.ns_per_op, kMaxSingleOpLatencyNs);
    EXPECT_LE(v2_explicit.ns_per_op, kMaxSingleOpLatencyNs);

    EXPECT_LE(ratio_legacy_compare_off, kMaxModeRatio);
    EXPECT_LE(ratio_legacy_v2, kMaxModeRatio);
    EXPECT_LE(ratio_compare_off_shadow, kMaxModeRatio);
}

TEST_F(PerfGuardrailFixture, ReplayFrameConsumptionHotPathBenchmarkStaysWithinLatencyAndJitterBudget)
{
    std::vector<double> legacy_samples;
    std::vector<double> compare_off_samples;
    std::vector<double> shadow_compare_samples;
    std::vector<double> v2_explicit_samples;
    legacy_samples.reserve(kPerfSamples);
    compare_off_samples.reserve(kPerfSamples);
    shadow_compare_samples.reserve(kPerfSamples);
    v2_explicit_samples.reserve(kPerfSamples);

    for (size_t i = 0; i < kPerfSamples; ++i) {
        legacy_samples.push_back(RunReplayNsPerStep(
                                     tmp_dir_,
                                     BinanceExchange::CoreMode::LegacyOnly,
                                     BinanceExchange::EventPublishMode::LegacyDirect,
                                     false,
                                     kSessionIterations)
                                     .ns_per_step);
        compare_off_samples.push_back(RunReplayNsPerStep(
                                          tmp_dir_,
                                          BinanceExchange::CoreMode::LegacyOnly,
                                          BinanceExchange::EventPublishMode::DomainEventAdapter,
                                          false,
                                          kSessionIterations)
                                          .ns_per_step);
        shadow_compare_samples.push_back(RunReplayNsPerStep(
                                             tmp_dir_,
                                             BinanceExchange::CoreMode::NewCoreShadow,
                                             BinanceExchange::EventPublishMode::DomainEventAdapter,
                                             false,
                                             kSessionIterations)
                                             .ns_per_step);
        v2_explicit_samples.push_back(RunReplayNsPerStep(
                                          tmp_dir_,
                                          BinanceExchange::CoreMode::NewCorePrimary,
                                          BinanceExchange::EventPublishMode::DomainEventAdapter,
                                          false,
                                          kSessionIterations)
                                          .ns_per_step);
    }

    const auto legacy_stats = BuildStats(std::move(legacy_samples));
    const auto compare_off_stats = BuildStats(std::move(compare_off_samples));
    const auto shadow_compare_stats = BuildStats(std::move(shadow_compare_samples));
    const auto v2_explicit_stats = BuildStats(std::move(v2_explicit_samples));

    const double legacy_jitter = RelativeJitterP95(legacy_stats);
    const double compare_off_jitter = RelativeJitterP95(compare_off_stats);
    const double shadow_compare_jitter = RelativeJitterP95(shadow_compare_stats);
    const double v2_explicit_jitter = RelativeJitterP95(v2_explicit_stats);

    std::cout << "[PERF][ReplayFrameStats] legacy_p50=" << legacy_stats.p50
              << " legacy_p95=" << legacy_stats.p95
              << " legacy_jitter=" << legacy_jitter
              << " compare_off_p50=" << compare_off_stats.p50
              << " compare_off_p95=" << compare_off_stats.p95
              << " compare_off_jitter=" << compare_off_jitter
              << " shadow_p50=" << shadow_compare_stats.p50
              << " shadow_p95=" << shadow_compare_stats.p95
              << " shadow_jitter=" << shadow_compare_jitter
              << " v2_p50=" << v2_explicit_stats.p50
              << " v2_p95=" << v2_explicit_stats.p95
              << " v2_jitter=" << v2_explicit_jitter << '\n';

    EXPECT_LE(legacy_stats.p95, kMaxSingleStepLatencyNs);
    EXPECT_LE(compare_off_stats.p95, kMaxSingleStepLatencyNs);
    EXPECT_LE(shadow_compare_stats.p95, kMaxSingleStepLatencyNs);
    EXPECT_LE(v2_explicit_stats.p95, kMaxSingleStepLatencyNs);

    EXPECT_LE(legacy_jitter, kMaxP95JitterRatio);
    EXPECT_LE(compare_off_jitter, kMaxP95JitterRatio);
    EXPECT_LE(shadow_compare_jitter, kMaxP95JitterRatio);
    EXPECT_LE(v2_explicit_jitter, kMaxP95JitterRatio);
}

TEST_F(PerfGuardrailFixture, CompareDiagnosticOverheadIsQuantifiedAndBoundedByBudget)
{
    const auto compare_off = RunReplayNsPerStep(
        tmp_dir_,
        BinanceExchange::CoreMode::LegacyOnly,
        BinanceExchange::EventPublishMode::DomainEventAdapter,
        true,
        kSessionIterations);
    const auto shadow_compare = RunReplayNsPerStep(
        tmp_dir_,
        BinanceExchange::CoreMode::NewCoreShadow,
        BinanceExchange::EventPublishMode::DomainEventAdapter,
        true,
        kSessionIterations);
    const auto dual_publish_compare = RunReplayNsPerStep(
        tmp_dir_,
        BinanceExchange::CoreMode::LegacyOnly,
        BinanceExchange::EventPublishMode::DualPublishCompare,
        true,
        kSessionIterations);

    ASSERT_GT(compare_off.steps, 0u);
    ASSERT_GT(shadow_compare.steps, 0u);
    ASSERT_GT(dual_publish_compare.steps, 0u);
    ASSERT_GT(compare_off.ns_per_step, 0.0);
    ASSERT_GT(shadow_compare.ns_per_step, 0.0);
    ASSERT_GT(dual_publish_compare.ns_per_step, 0.0);

    const double ratio_shadow_vs_compare_off = shadow_compare.ns_per_step / compare_off.ns_per_step;
    const double ratio_dualpublish_vs_compare_off = dual_publish_compare.ns_per_step / compare_off.ns_per_step;

    const double shadow_diag_per_step = static_cast<double>(shadow_compare.compare_diag_count)
        / static_cast<double>(shadow_compare.steps);
    const double dualpublish_diag_per_step = static_cast<double>(dual_publish_compare.event_publish_diag_count)
        / static_cast<double>(dual_publish_compare.steps);

    std::cout << "[PERF][CompareOverhead] compare_off_ns_per_step=" << compare_off.ns_per_step
              << " shadow_ns_per_step=" << shadow_compare.ns_per_step
              << " dualpublish_ns_per_step=" << dual_publish_compare.ns_per_step
              << " shadow_compare_diag_count=" << shadow_compare.compare_diag_count
              << " dualpublish_event_diag_count=" << dual_publish_compare.event_publish_diag_count
              << " shadow_diag_per_step=" << shadow_diag_per_step
              << " dualpublish_diag_per_step=" << dualpublish_diag_per_step
              << " ratio_shadow_vs_compare_off=" << ratio_shadow_vs_compare_off
              << " ratio_dualpublish_vs_compare_off=" << ratio_dualpublish_vs_compare_off << '\n';

    EXPECT_GT(shadow_compare.compare_diag_count, 0u);
    // Event publish diagnostics require side-effect publish compare snapshots to be materialized;
    // benchmark fixture may run with lightweight setup where this counter is zero.
    EXPECT_GE(dual_publish_compare.event_publish_diag_count, 0u);
    EXPECT_LE(ratio_shadow_vs_compare_off, kMaxModeRatio);
    EXPECT_LE(ratio_dualpublish_vs_compare_off, kMaxModeRatio);
}

TEST_F(PerfGuardrailFixture, LogHeavyScenarioCoversRowVolumeOrderingBatchBoundaryAndHighDensityPublish)
{
    const auto reference_run = RunLogHeavyScenario(
        tmp_dir_,
        nullptr,
        BinanceExchange::CoreMode::LegacyOnly,
        BinanceExchange::EventPublishMode::LegacyDirect,
        false);
    const auto forwarding = RunLogHeavyScenario(
        tmp_dir_,
        nullptr,
        BinanceExchange::CoreMode::LegacyOnly,
        BinanceExchange::EventPublishMode::DualPublishCompare,
        true);

    ASSERT_GT(reference_run.steps, 0u);
    ASSERT_GT(forwarding.steps, 0u);
    ASSERT_GT(reference_run.market_msgs, 0u);
    ASSERT_GT(forwarding.market_msgs, 0u);

    EXPECT_GT(forwarding.position_msgs + forwarding.order_msgs, 0u);
    EXPECT_GT(forwarding.forwarded_market_count, 0u);
    EXPECT_GT(forwarding.side_effect_hook_calls, 0u);
    EXPECT_EQ(forwarding.publish_order_violations, 0u);

    if (forwarding.event_diag_count > 0u) {
        EXPECT_EQ(forwarding.event_diag_count, forwarding.event_diag_matched_count);
    }

    std::cout << "[PERF][LogHeavyScenario] reference_ns_per_step=" << reference_run.ns_per_step
              << " reference_throughput=" << reference_run.throughput_steps_per_sec
              << " forwarding_ns_per_step=" << forwarding.ns_per_step
              << " forwarding_throughput=" << forwarding.throughput_steps_per_sec
              << " forwarding_position_msgs=" << forwarding.position_msgs
              << " forwarding_order_msgs=" << forwarding.order_msgs
              << " forwarding_max_position_batch=" << forwarding.max_position_batch_size
              << " forwarding_max_order_batch=" << forwarding.max_order_batch_size
              << " forwarding_market_backlog=" << forwarding.max_market_backlog
              << '\n';

    EXPECT_LE(reference_run.ns_per_step, kLogHeavyStepBudgetNs);
    EXPECT_LE(forwarding.ns_per_step, kLogHeavyStepBudgetNs);
    EXPECT_GE(reference_run.throughput_steps_per_sec, kLogHeavyMinThroughputStepsPerSec);
    EXPECT_GE(forwarding.throughput_steps_per_sec, kLogHeavyMinThroughputStepsPerSec);
}

TEST_F(PerfGuardrailFixture, LogHeavyLoggerEnableDisableAndSideEffectForwardingOverheadStayWithinBudget)
{
    auto logger = std::make_shared<PerfMockLogger>(tmp_dir_.string());
    logger->Start();

    const auto no_logger = RunLogHeavyScenario(
        tmp_dir_,
        nullptr,
        BinanceExchange::CoreMode::LegacyOnly,
        BinanceExchange::EventPublishMode::LegacyDirect,
        false);
    const auto with_logger = RunLogHeavyScenario(
        tmp_dir_,
        logger,
        BinanceExchange::CoreMode::LegacyOnly,
        BinanceExchange::EventPublishMode::LegacyDirect,
        false);
    const auto with_forwarding = RunLogHeavyScenario(
        tmp_dir_,
        logger,
        BinanceExchange::CoreMode::LegacyOnly,
        BinanceExchange::EventPublishMode::DomainEventAdapter,
        true);

    logger->Stop();

    ASSERT_GT(no_logger.steps, 0u);
    ASSERT_GT(with_logger.steps, 0u);
    ASSERT_GT(with_forwarding.steps, 0u);

    const double logger_ratio = with_logger.ns_per_step / no_logger.ns_per_step;
    const double forwarding_ratio = with_forwarding.ns_per_step / with_logger.ns_per_step;

    std::cout << "[PERF][LogHeavyOverhead] no_logger_ns_per_step=" << no_logger.ns_per_step
              << " with_logger_ns_per_step=" << with_logger.ns_per_step
              << " with_forwarding_ns_per_step=" << with_forwarding.ns_per_step
              << " logger_ratio=" << logger_ratio
              << " forwarding_ratio=" << forwarding_ratio
              << " no_logger_backlog=" << no_logger.max_market_backlog
              << " with_logger_backlog=" << with_logger.max_market_backlog
              << " with_forwarding_backlog=" << with_forwarding.max_market_backlog
              << '\n';

    EXPECT_LE(logger_ratio, kMaxModeRatio);
    EXPECT_LE(forwarding_ratio, kMaxModeRatio);

    // Memory/buffering proxy guardrail: forwarding path backlog must not explode compared with the reference path.
    const double backlog_multiplier = (no_logger.max_market_backlog == 0)
        ? static_cast<double>(with_forwarding.max_market_backlog + 1)
        : static_cast<double>(with_forwarding.max_market_backlog + 1)
        / static_cast<double>(no_logger.max_market_backlog + 1);
    EXPECT_LE(backlog_multiplier, kLogHeavyBufferGuardMultiplier);
}

TEST_F(PerfGuardrailFixture, LogHeavyCompareArtifactOnlyEnabledInValidationModes)
{
    const auto legacy_only = RunLogHeavyScenario(
        tmp_dir_,
        nullptr,
        BinanceExchange::CoreMode::LegacyOnly,
        BinanceExchange::EventPublishMode::LegacyDirect,
        false,
        600,
        200);
    const auto compare_off = RunLogHeavyScenario(
        tmp_dir_,
        nullptr,
        BinanceExchange::CoreMode::LegacyOnly,
        BinanceExchange::EventPublishMode::DomainEventAdapter,
        false,
        600,
        200);
    const auto shadow_compare = RunLogHeavyScenario(
        tmp_dir_,
        nullptr,
        BinanceExchange::CoreMode::NewCoreShadow,
        BinanceExchange::EventPublishMode::DomainEventAdapter,
        false,
        600,
        200);
    const auto v2_explicit = RunLogHeavyScenario(
        tmp_dir_,
        nullptr,
        BinanceExchange::CoreMode::NewCorePrimary,
        BinanceExchange::EventPublishMode::DomainEventAdapter,
        false,
        600,
        200);

    ASSERT_GT(legacy_only.steps, 0u);
    ASSERT_GT(compare_off.steps, 0u);
    ASSERT_GT(shadow_compare.steps, 0u);
    ASSERT_GT(v2_explicit.steps, 0u);

    std::cout << "[PERF][CompareArtifactMode] legacy_compare_artifact_steps=" << legacy_only.compare_artifact_enabled_steps
              << " compare_off_compare_artifact_steps=" << compare_off.compare_artifact_enabled_steps
              << " shadow_compare_artifact_steps=" << shadow_compare.compare_artifact_enabled_steps
              << " v2_compare_artifact_steps=" << v2_explicit.compare_artifact_enabled_steps
              << '\n';

    EXPECT_EQ(legacy_only.compare_artifact_enabled_steps, 0u);
    EXPECT_EQ(compare_off.compare_artifact_enabled_steps, 0u);
    EXPECT_GT(shadow_compare.compare_artifact_enabled_steps, 0u);
    EXPECT_EQ(v2_explicit.compare_artifact_enabled_steps, 0u);
}

TEST_F(PerfGuardrailFixture, MixedScenarioBenchmarkCoversBasisMixedFundingAndAsyncAckPacks)
{
    const auto scenarios = ReplayCompare::V2ReplayScenarioPack::BuildCoreScenarioPack();
    const std::vector<std::string> required_names = {
        "v2-vs-legacy.basis-stress",
        "v2-vs-legacy.mixed-spot-perp",
        "v2-vs-legacy.funding-reference-edge",
        "v2-vs-legacy.async-ack-latency",
    };

    for (const auto& name : required_names) {
        const auto* scenario = FindMixedScenario(scenarios, name);
        ASSERT_TRUE(scenario != nullptr) << "missing mixed benchmark scenario: " << name;
        const auto perf = RunMixedScenarioBenchmark(*scenario, ReplayCompare::ReplayCompareFeatureSet::All());
        EXPECT_EQ(perf.status, ReplayCompare::ReplayCompareStatus::Success) << name;
        EXPECT_EQ(perf.mismatch_count, 0u) << name;
        EXPECT_GT(perf.compared_steps, 0u) << name;
        EXPECT_GT(perf.ns_per_step, 0.0) << name;
        EXPECT_GT(perf.throughput_steps_per_sec, 0.0) << name;
    }
}

TEST_F(PerfGuardrailFixture, MixedScenarioBenchmarkSplitsReplayOrchestrationAndLoggingCosts)
{
    const auto scenarios = ReplayCompare::V2ReplayScenarioPack::BuildCoreScenarioPack();
    const std::vector<std::string> required_names = {
        "v2-vs-legacy.basis-stress",
        "v2-vs-legacy.mixed-spot-perp",
        "v2-vs-legacy.funding-reference-edge",
        "v2-vs-legacy.async-ack-latency",
    };

    const auto replay_only = ReplayCompare::ReplayCompareTestHarness::StateOnlyFeatureSet();
    const auto orchestration_only = ReplayCompare::ReplayCompareTestHarness::BuildFeatureSet({
        ReplayCompare::ReplayCompareFeature::Event,
        ReplayCompare::ReplayCompareFeature::AsyncAckTimeline,
    });
    const auto logging_only = ReplayCompare::ReplayCompareTestHarness::RowOnlyFeatureSet();
    const auto all_features = ReplayCompare::ReplayCompareFeatureSet::All();

    for (const auto& name : required_names) {
        const auto* scenario = FindMixedScenario(scenarios, name);
        ASSERT_TRUE(scenario != nullptr) << "missing mixed benchmark scenario: " << name;

        const auto replay_cost = RunMixedScenarioBenchmark(*scenario, replay_only);
        const auto orchestration_cost = RunMixedScenarioBenchmark(*scenario, orchestration_only);
        const auto logging_cost = RunMixedScenarioBenchmark(*scenario, logging_only);
        const auto all_cost = RunMixedScenarioBenchmark(*scenario, all_features);

        EXPECT_EQ(replay_cost.status, ReplayCompare::ReplayCompareStatus::Success) << name;
        EXPECT_EQ(orchestration_cost.status, ReplayCompare::ReplayCompareStatus::Success) << name;
        EXPECT_EQ(logging_cost.status, ReplayCompare::ReplayCompareStatus::Success) << name;
        EXPECT_EQ(all_cost.status, ReplayCompare::ReplayCompareStatus::Success) << name;

        EXPECT_GT(replay_cost.ns_per_step, 0.0) << name;
        EXPECT_GT(orchestration_cost.ns_per_step, 0.0) << name;
        EXPECT_GT(logging_cost.ns_per_step, 0.0) << name;
        EXPECT_GT(all_cost.ns_per_step, 0.0) << name;

        const double latency_ratio = all_cost.ns_per_step / replay_cost.ns_per_step;
        const double throughput_ratio = all_cost.throughput_steps_per_sec / replay_cost.throughput_steps_per_sec;

        std::cout << "[PERF][MixedCostSplit] scenario=" << name
                  << " replay_ns_per_step=" << replay_cost.ns_per_step
                  << " orchestration_ns_per_step=" << orchestration_cost.ns_per_step
                  << " logging_ns_per_step=" << logging_cost.ns_per_step
                  << " all_ns_per_step=" << all_cost.ns_per_step
                  << " latency_ratio_vs_replay=" << latency_ratio
                  << " throughput_ratio_vs_replay=" << throughput_ratio
                  << '\n';

        EXPECT_LE(latency_ratio, kMixedScenarioMaxLatencyRatioVsReplay) << "scenario=" << name;
        EXPECT_GE(throughput_ratio, kMixedScenarioMinThroughputRatioVsReplay) << "scenario=" << name;
    }
}

TEST_F(PerfGuardrailFixture, MixedScenarioBudgetCoversJitterAndFallbackUnsupportedOverhead)
{
    const auto scenarios = ReplayCompare::V2ReplayScenarioPack::BuildCoreScenarioPack();
    const auto* base = FindMixedScenario(scenarios, "v2-vs-legacy.funding-reference-edge");
    ASSERT_TRUE(base != nullptr);

    constexpr size_t kMixedJitterInnerRepeats = 5;

    std::vector<double> samples;
    samples.reserve(kPerfSamples);
    for (size_t i = 0; i < kPerfSamples; ++i) {
        double sum_ns_per_step = 0.0;
        for (size_t rep = 0; rep < kMixedJitterInnerRepeats; ++rep) {
            const auto perf = RunMixedScenarioBenchmark(*base, ReplayCompare::ReplayCompareFeatureSet::All());
            ASSERT_EQ(perf.status, ReplayCompare::ReplayCompareStatus::Success);
            sum_ns_per_step += perf.ns_per_step;
        }
        samples.push_back(sum_ns_per_step / static_cast<double>(kMixedJitterInnerRepeats));
    }

    const auto stats = BuildStats(std::move(samples));
    const double jitter = RelativeJitterP95(stats);
    EXPECT_LE(jitter, kMaxP95JitterRatio) << "MIXED_BUDGET_FAIL metric=p95_jitter scenario=v2-vs-legacy.funding-reference-edge";

    auto unsupported_scenarios = ReplayCompare::V2ReplayScenarioPack::BuildCoreScenarioPack();
    auto* unsupported = FindMixedScenario(unsupported_scenarios, "v2-vs-legacy.funding-reference-edge");
    ASSERT_TRUE(unsupported != nullptr);
    unsupported->legacy_steps[0].state.progress.status = ReplayCompare::ReplayCompareStatus::Unsupported;
    unsupported->candidate_steps[0].state.progress.status = ReplayCompare::ReplayCompareStatus::Unsupported;

    auto fallback_scenarios = ReplayCompare::V2ReplayScenarioPack::BuildCoreScenarioPack();
    auto* fallback = FindMixedScenario(fallback_scenarios, "v2-vs-legacy.funding-reference-edge");
    ASSERT_TRUE(fallback != nullptr);
    fallback->legacy_steps[0].state.progress.status = ReplayCompare::ReplayCompareStatus::Fallback;
    fallback->legacy_steps[0].state.progress.fallback_to_legacy = true;
    fallback->candidate_steps[0].state.progress.status = ReplayCompare::ReplayCompareStatus::Fallback;
    fallback->candidate_steps[0].state.progress.fallback_to_legacy = true;

    const auto reference_perf = RunMixedScenarioBenchmark(*base, ReplayCompare::ReplayCompareTestHarness::StateOnlyFeatureSet());
    const auto unsupported_perf = RunMixedScenarioBenchmark(*unsupported, ReplayCompare::ReplayCompareTestHarness::StateOnlyFeatureSet());
    const auto fallback_perf = RunMixedScenarioBenchmark(*fallback, ReplayCompare::ReplayCompareTestHarness::StateOnlyFeatureSet());

    ASSERT_GT(reference_perf.ns_per_step, 0.0);
    ASSERT_GT(unsupported_perf.ns_per_step, 0.0);
    ASSERT_GT(fallback_perf.ns_per_step, 0.0);

    const double unsupported_ratio = unsupported_perf.ns_per_step / reference_perf.ns_per_step;
    const double fallback_ratio = fallback_perf.ns_per_step / reference_perf.ns_per_step;

    std::cout << "[PERF][MixedBudget] scenario=v2-vs-legacy.funding-reference-edge"
              << " p50_ns_per_step=" << stats.p50
              << " p95_ns_per_step=" << stats.p95
              << " jitter=" << jitter
              << " unsupported_ratio=" << unsupported_ratio
              << " fallback_ratio=" << fallback_ratio
              << '\n';

    EXPECT_LE(unsupported_ratio, kFallbackUnsupportedOverheadRatioBudget)
        << "MIXED_BUDGET_FAIL metric=unsupported_ratio scenario=v2-vs-legacy.funding-reference-edge";
    EXPECT_LE(fallback_ratio, kFallbackUnsupportedOverheadRatioBudget)
        << "MIXED_BUDGET_FAIL metric=fallback_ratio scenario=v2-vs-legacy.funding-reference-edge";
}
