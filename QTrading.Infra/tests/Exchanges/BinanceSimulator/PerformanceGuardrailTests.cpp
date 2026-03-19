#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "Dto/Market/Binance/Kline.hpp"
#include "Dto/Trading/Side.hpp"
#include "Exchanges/BinanceSimulator/Account/Account.hpp"
#include "Exchanges/BinanceSimulator/Account/AccountCoreV2.hpp"
#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

using namespace QTrading::Infra::Exchanges::BinanceSim;
using QTrading::Dto::Market::Binance::TradeKlineDto;
using QTrading::Dto::Trading::OrderSide;
using QTrading::Dto::Trading::PositionSide;

namespace fs = std::filesystem;

namespace {

constexpr size_t kPerfWarmupRounds = 1;
constexpr size_t kHotPathIterations = 1600;
constexpr size_t kReplayRows = 1200;

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
};

ReplayPerfResult RunReplayNsPerStep(
    const fs::path& tmp_dir,
    BinanceExchange::CoreMode core_mode,
    BinanceExchange::EventPublishMode publish_mode)
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
        ++steps;
    }
    const auto end = std::chrono::steady_clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    ReplayPerfResult result{};
    result.steps = steps;
    result.market_msgs = market_msgs;
    result.ns_per_step = (steps == 0) ? 0.0 : static_cast<double>(ns) / static_cast<double>(steps);
    return result;
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

