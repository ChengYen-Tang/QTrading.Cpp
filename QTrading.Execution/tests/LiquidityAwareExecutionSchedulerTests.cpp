#include "Execution/LiquidityAwareExecutionScheduler.hpp"

#include <gtest/gtest.h>

#include <memory>

using MarketPtr = std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>;

namespace {

MarketPtr MakeMarketWithSymbol(
    uint64_t ts,
    const std::string& symbol,
    double close,
    double quote_volume)
{
    auto market = std::make_shared<QTrading::Dto::Market::Binance::MultiKlineDto>();
    market->Timestamp = ts;
    auto symbols = std::make_shared<std::vector<std::string>>();
    symbols->push_back(symbol);
    market->symbols = symbols;
    market->trade_klines_by_id.resize(1);
    market->trade_klines_by_id[0] = QTrading::Dto::Market::Binance::KlineDto(
        ts,
        0,
        0,
        0,
        close,
        0,
        ts,
        quote_volume,
        0,
        0,
        0);
    return market;
}

} // namespace

TEST(LiquidityAwareExecutionSchedulerTests, CapsCarryDeltaByQuoteVolumeParticipation)
{
    QTrading::Execution::LiquidityAwareExecutionScheduler::Config cfg;
    cfg.carry_delta_participation_cap_enabled = true;
    cfg.carry_delta_participation_rate = 0.10; // max delta notional = quote_volume * 10%
    cfg.carry_min_slice_notional_usdt = 0.0;
    cfg.carry_apply_only_low_urgency = true;
    cfg.include_open_orders_in_current_notional = false;

    QTrading::Execution::LiquidityAwareExecutionScheduler scheduler(cfg);

    std::vector<QTrading::Execution::ExecutionParentOrder> parent_orders = {
        QTrading::Execution::ExecutionParentOrder{
            1,
            "BTCUSDT_PERP",
            1000.0,
            2.0,
        },
    };

    QTrading::Risk::AccountState account;

    QTrading::Execution::ExecutionSignal signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Execution::ExecutionUrgency::Low;

    auto market = MakeMarketWithSymbol(1, "BTCUSDT_PERP", 10'000.0, 500.0);
    // max delta notional = 500 * 0.1 = 50 => target should become 50, not 1000.
    const auto slices = scheduler.BuildSlices(parent_orders, account, signal, market);
    ASSERT_EQ(slices.size(), 1u);
    EXPECT_DOUBLE_EQ(slices[0].target_notional, 50.0);
}

TEST(LiquidityAwareExecutionSchedulerTests, BasisArbitrageAlsoUsesParticipationCap)
{
    QTrading::Execution::LiquidityAwareExecutionScheduler::Config cfg;
    cfg.carry_delta_participation_cap_enabled = true;
    cfg.carry_delta_participation_rate = 0.10;
    cfg.carry_min_slice_notional_usdt = 0.0;
    cfg.carry_apply_only_low_urgency = true;
    cfg.include_open_orders_in_current_notional = false;

    QTrading::Execution::LiquidityAwareExecutionScheduler scheduler(cfg);

    std::vector<QTrading::Execution::ExecutionParentOrder> parent_orders = {
        QTrading::Execution::ExecutionParentOrder{
            1,
            "BTCUSDT_PERP",
            1000.0,
            2.0,
        },
    };

    QTrading::Risk::AccountState account;
    QTrading::Execution::ExecutionSignal signal;
    signal.strategy = "basis_arbitrage";
    signal.urgency = QTrading::Execution::ExecutionUrgency::Low;

    const auto slices = scheduler.BuildSlices(
        parent_orders,
        account,
        signal,
        MakeMarketWithSymbol(1, "BTCUSDT_PERP", 10'000.0, 500.0));
    ASSERT_EQ(slices.size(), 1u);
    EXPECT_DOUBLE_EQ(slices[0].target_notional, 50.0);
}

TEST(LiquidityAwareExecutionSchedulerTests, UsesCurrentNotionalFromPositionWhenClippingDelta)
{
    QTrading::Execution::LiquidityAwareExecutionScheduler::Config cfg;
    cfg.carry_delta_participation_cap_enabled = true;
    cfg.carry_delta_participation_rate = 0.10;
    cfg.carry_min_slice_notional_usdt = 0.0;
    cfg.carry_apply_only_low_urgency = true;
    cfg.include_open_orders_in_current_notional = false;

    QTrading::Execution::LiquidityAwareExecutionScheduler scheduler(cfg);

    std::vector<QTrading::Execution::ExecutionParentOrder> parent_orders = {
        QTrading::Execution::ExecutionParentOrder{
            1,
            "BTCUSDT_PERP",
            1200.0,
            2.0,
        },
    };

    QTrading::Risk::AccountState account;
    QTrading::dto::Position position{};
    position.symbol = "BTCUSDT_PERP";
    position.quantity = 0.1; // current notional = 1000 at close 10,000
    position.is_long = true;
    account.positions.push_back(position);

    QTrading::Execution::ExecutionSignal signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Execution::ExecutionUrgency::Low;

    auto market = MakeMarketWithSymbol(1, "BTCUSDT_PERP", 10'000.0, 500.0);
    // raw delta = 1200 - 1000 = 200; clipped to 50 => slice target should be 1050.
    const auto slices = scheduler.BuildSlices(parent_orders, account, signal, market);
    ASSERT_EQ(slices.size(), 1u);
    EXPECT_DOUBLE_EQ(slices[0].target_notional, 1050.0);
}

TEST(LiquidityAwareExecutionSchedulerTests, ConfidenceAdaptiveRateScalesClippedDelta)
{
    QTrading::Execution::LiquidityAwareExecutionScheduler::Config cfg;
    cfg.carry_delta_participation_cap_enabled = true;
    cfg.carry_delta_participation_rate = 0.10;
    cfg.carry_min_slice_notional_usdt = 0.0;
    cfg.carry_apply_only_low_urgency = true;
    cfg.include_open_orders_in_current_notional = false;
    cfg.carry_confidence_adaptive_enabled = true;
    cfg.carry_confidence_rate_scale_min = 0.5;
    cfg.carry_confidence_rate_scale_max = 1.5;

    QTrading::Execution::LiquidityAwareExecutionScheduler low_scheduler(cfg);
    QTrading::Execution::LiquidityAwareExecutionScheduler high_scheduler(cfg);

    std::vector<QTrading::Execution::ExecutionParentOrder> parent_orders = {
        QTrading::Execution::ExecutionParentOrder{
            1,
            "BTCUSDT_PERP",
            1000.0,
            2.0,
        },
    };
    QTrading::Risk::AccountState account;
    auto market = MakeMarketWithSymbol(1, "BTCUSDT_PERP", 10'000.0, 1'000.0);

    QTrading::Execution::ExecutionSignal low_signal;
    low_signal.strategy = "funding_carry";
    low_signal.urgency = QTrading::Execution::ExecutionUrgency::Low;
    low_signal.confidence = 0.0;

    QTrading::Execution::ExecutionSignal high_signal = low_signal;
    high_signal.confidence = 1.0;

    const auto low_slices = low_scheduler.BuildSlices(parent_orders, account, low_signal, market);
    const auto high_slices = high_scheduler.BuildSlices(parent_orders, account, high_signal, market);
    ASSERT_EQ(low_slices.size(), 1u);
    ASSERT_EQ(high_slices.size(), 1u);
    // low: 1000 * (0.10*0.5) = 50
    EXPECT_DOUBLE_EQ(low_slices[0].target_notional, 50.0);
    // high: 1000 * (0.10*1.5) = 150
    EXPECT_DOUBLE_EQ(high_slices[0].target_notional, 150.0);
}

TEST(LiquidityAwareExecutionSchedulerTests, WindowBudgetCapsCumulativeDeltaAndResetsNextWindow)
{
    QTrading::Execution::LiquidityAwareExecutionScheduler::Config cfg;
    cfg.carry_delta_participation_cap_enabled = false;
    cfg.carry_window_budget_enabled = true;
    cfg.carry_window_budget_ms = 1000;
    cfg.carry_window_max_notional_usdt = 100.0;
    cfg.carry_window_quote_participation_rate = 0.0;
    cfg.carry_min_slice_notional_usdt = 0.0;

    QTrading::Execution::LiquidityAwareExecutionScheduler scheduler(cfg);

    std::vector<QTrading::Execution::ExecutionParentOrder> parent_orders = {
        QTrading::Execution::ExecutionParentOrder{
            1,
            "BTCUSDT_PERP",
            1000.0,
            2.0,
        },
    };
    QTrading::Risk::AccountState account;
    QTrading::Execution::ExecutionSignal signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Execution::ExecutionUrgency::Low;

    const auto s1 = scheduler.BuildSlices(
        parent_orders,
        account,
        signal,
        MakeMarketWithSymbol(100, "BTCUSDT_PERP", 10'000.0, 10'000.0));
    ASSERT_EQ(s1.size(), 1u);
    EXPECT_DOUBLE_EQ(s1[0].target_notional, 100.0);

    const auto s2 = scheduler.BuildSlices(
        parent_orders,
        account,
        signal,
        MakeMarketWithSymbol(200, "BTCUSDT_PERP", 10'000.0, 10'000.0));
    ASSERT_EQ(s2.size(), 1u);
    EXPECT_DOUBLE_EQ(s2[0].target_notional, 0.0);

    // New window (ts 1100 -> key 1) should restore budget.
    const auto s3 = scheduler.BuildSlices(
        parent_orders,
        account,
        signal,
        MakeMarketWithSymbol(1100, "BTCUSDT_PERP", 10'000.0, 10'000.0));
    ASSERT_EQ(s3.size(), 1u);
    EXPECT_DOUBLE_EQ(s3[0].target_notional, 100.0);
}

TEST(LiquidityAwareExecutionSchedulerTests, WindowBudgetUsesCumulativeQuoteVolumeParticipation)
{
    QTrading::Execution::LiquidityAwareExecutionScheduler::Config cfg;
    cfg.carry_delta_participation_cap_enabled = false;
    cfg.carry_window_budget_enabled = true;
    cfg.carry_window_budget_ms = 10'000;
    cfg.carry_window_max_notional_usdt = 0.0;
    cfg.carry_window_quote_participation_rate = 0.10;
    cfg.carry_min_slice_notional_usdt = 0.0;

    QTrading::Execution::LiquidityAwareExecutionScheduler scheduler(cfg);
    std::vector<QTrading::Execution::ExecutionParentOrder> parent_orders = {
        QTrading::Execution::ExecutionParentOrder{
            1,
            "BTCUSDT_PERP",
            1000.0,
            2.0,
        },
    };
    QTrading::Risk::AccountState account;
    QTrading::Execution::ExecutionSignal signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Execution::ExecutionUrgency::Low;

    const auto s1 = scheduler.BuildSlices(
        parent_orders,
        account,
        signal,
        MakeMarketWithSymbol(100, "BTCUSDT_PERP", 10'000.0, 500.0));
    ASSERT_EQ(s1.size(), 1u);
    // cumulative qv=500 => cap=50
    EXPECT_DOUBLE_EQ(s1[0].target_notional, 50.0);

    const auto s2 = scheduler.BuildSlices(
        parent_orders,
        account,
        signal,
        MakeMarketWithSymbol(200, "BTCUSDT_PERP", 10'000.0, 500.0));
    ASSERT_EQ(s2.size(), 1u);
    // cumulative qv=1000 => cap=100, consumed=50 => remaining=50
    EXPECT_DOUBLE_EQ(s2[0].target_notional, 50.0);
}

TEST(LiquidityAwareExecutionSchedulerTests, IncreaseBatchingHoldsTargetWithinBatchWindow)
{
    QTrading::Execution::LiquidityAwareExecutionScheduler::Config cfg;
    cfg.carry_increase_batching_enabled = true;
    cfg.carry_increase_batch_ms = 1000;
    cfg.carry_apply_only_low_urgency = true;

    QTrading::Execution::LiquidityAwareExecutionScheduler scheduler(cfg);
    std::vector<QTrading::Execution::ExecutionParentOrder> parent_orders = {
        QTrading::Execution::ExecutionParentOrder{
            1,
            "BTCUSDT_PERP",
            1000.0,
            2.0,
        },
    };
    QTrading::Risk::AccountState account;
    QTrading::Execution::ExecutionSignal signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Execution::ExecutionUrgency::Low;

    const auto s1 = scheduler.BuildSlices(
        parent_orders,
        account,
        signal,
        MakeMarketWithSymbol(100, "BTCUSDT_PERP", 10'000.0, 1000.0));
    ASSERT_EQ(s1.size(), 1u);
    EXPECT_DOUBLE_EQ(s1[0].target_notional, 1000.0);

    parent_orders[0].target_notional = 1200.0;
    const auto s2 = scheduler.BuildSlices(
        parent_orders,
        account,
        signal,
        MakeMarketWithSymbol(200, "BTCUSDT_PERP", 10'000.0, 1000.0));
    ASSERT_EQ(s2.size(), 1u);
    // Increase request within batch window should be held.
    EXPECT_DOUBLE_EQ(s2[0].target_notional, 1000.0);

    const auto s3 = scheduler.BuildSlices(
        parent_orders,
        account,
        signal,
        MakeMarketWithSymbol(1200, "BTCUSDT_PERP", 10'000.0, 1000.0));
    ASSERT_EQ(s3.size(), 1u);
    EXPECT_DOUBLE_EQ(s3[0].target_notional, 1200.0);
}

TEST(LiquidityAwareExecutionSchedulerTests, IncreaseBatchingDoesNotDelayTargetReduction)
{
    QTrading::Execution::LiquidityAwareExecutionScheduler::Config cfg;
    cfg.carry_increase_batching_enabled = true;
    cfg.carry_increase_batch_ms = 1000;
    cfg.carry_apply_only_low_urgency = true;

    QTrading::Execution::LiquidityAwareExecutionScheduler scheduler(cfg);
    std::vector<QTrading::Execution::ExecutionParentOrder> parent_orders = {
        QTrading::Execution::ExecutionParentOrder{
            1,
            "BTCUSDT_PERP",
            1000.0,
            2.0,
        },
    };
    QTrading::Risk::AccountState account;
    QTrading::dto::Position position{};
    position.symbol = "BTCUSDT_PERP";
    position.quantity = 0.1; // current notional = 1000 at close 10,000
    position.is_long = true;
    account.positions.push_back(position);
    QTrading::Execution::ExecutionSignal signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Execution::ExecutionUrgency::Low;

    const auto s1 = scheduler.BuildSlices(
        parent_orders,
        account,
        signal,
        MakeMarketWithSymbol(100, "BTCUSDT_PERP", 10'000.0, 1000.0));
    ASSERT_EQ(s1.size(), 1u);
    EXPECT_DOUBLE_EQ(s1[0].target_notional, 1000.0);

    parent_orders[0].target_notional = 600.0;
    const auto s2 = scheduler.BuildSlices(
        parent_orders,
        account,
        signal,
        MakeMarketWithSymbol(200, "BTCUSDT_PERP", 10'000.0, 1000.0));
    ASSERT_EQ(s2.size(), 1u);
    // Reduction should pass through immediately.
    EXPECT_DOUBLE_EQ(s2[0].target_notional, 600.0);
}

TEST(LiquidityAwareExecutionSchedulerTests, IncreaseBatchingIgnoresTinyTargetChangeBelowThreshold)
{
    QTrading::Execution::LiquidityAwareExecutionScheduler::Config cfg;
    cfg.carry_increase_batching_enabled = true;
    cfg.carry_increase_batch_ms = 1000;
    cfg.carry_increase_batch_min_update_notional = 50.0;
    cfg.carry_apply_only_low_urgency = true;

    QTrading::Execution::LiquidityAwareExecutionScheduler scheduler(cfg);
    std::vector<QTrading::Execution::ExecutionParentOrder> parent_orders = {
        QTrading::Execution::ExecutionParentOrder{
            1,
            "BTCUSDT_PERP",
            1000.0,
            2.0,
        },
    };
    QTrading::Risk::AccountState account;
    QTrading::Execution::ExecutionSignal signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Execution::ExecutionUrgency::Low;

    const auto s1 = scheduler.BuildSlices(
        parent_orders,
        account,
        signal,
        MakeMarketWithSymbol(100, "BTCUSDT_PERP", 10'000.0, 1000.0));
    ASSERT_EQ(s1.size(), 1u);
    EXPECT_DOUBLE_EQ(s1[0].target_notional, 1000.0);

    // Only a 20 notional change (< 50 threshold), should be ignored.
    parent_orders[0].target_notional = 980.0;
    const auto s2 = scheduler.BuildSlices(
        parent_orders,
        account,
        signal,
        MakeMarketWithSymbol(200, "BTCUSDT_PERP", 10'000.0, 1000.0));
    ASSERT_EQ(s2.size(), 1u);
    EXPECT_DOUBLE_EQ(s2[0].target_notional, 1000.0);
}

TEST(LiquidityAwareExecutionSchedulerTests, IncreaseBatchTimerIsNotResetByTargetReduction)
{
    QTrading::Execution::LiquidityAwareExecutionScheduler::Config cfg;
    cfg.carry_increase_batching_enabled = true;
    cfg.carry_increase_batch_ms = 1000;
    cfg.carry_apply_only_low_urgency = true;

    QTrading::Execution::LiquidityAwareExecutionScheduler scheduler(cfg);
    std::vector<QTrading::Execution::ExecutionParentOrder> parent_orders = {
        QTrading::Execution::ExecutionParentOrder{
            1,
            "BTCUSDT_PERP",
            1000.0,
            2.0,
        },
    };
    QTrading::Risk::AccountState account;
    QTrading::dto::Position position{};
    position.symbol = "BTCUSDT_PERP";
    position.quantity = 0.1; // current notional = 1000 at close 10,000
    position.is_long = true;
    account.positions.push_back(position);
    QTrading::Execution::ExecutionSignal signal;
    signal.strategy = "funding_carry";
    signal.urgency = QTrading::Execution::ExecutionUrgency::Low;

    const auto s1 = scheduler.BuildSlices(
        parent_orders,
        account,
        signal,
        MakeMarketWithSymbol(100, "BTCUSDT_PERP", 10'000.0, 1000.0));
    ASSERT_EQ(s1.size(), 1u);
    EXPECT_DOUBLE_EQ(s1[0].target_notional, 1000.0);

    // First increase is accepted and starts batch timer.
    parent_orders[0].target_notional = 1200.0;
    const auto s2 = scheduler.BuildSlices(
        parent_orders,
        account,
        signal,
        MakeMarketWithSymbol(1200, "BTCUSDT_PERP", 10'000.0, 1000.0));
    ASSERT_EQ(s2.size(), 1u);
    EXPECT_DOUBLE_EQ(s2[0].target_notional, 1200.0);

    // Reduction should pass immediately but must not reset increase timer.
    parent_orders[0].target_notional = 800.0;
    const auto s3 = scheduler.BuildSlices(
        parent_orders,
        account,
        signal,
        MakeMarketWithSymbol(1300, "BTCUSDT_PERP", 10'000.0, 1000.0));
    ASSERT_EQ(s3.size(), 1u);
    EXPECT_DOUBLE_EQ(s3[0].target_notional, 800.0);

    // Too early since last increase (t=1200), so still held at 800.
    parent_orders[0].target_notional = 1100.0;
    const auto s4 = scheduler.BuildSlices(
        parent_orders,
        account,
        signal,
        MakeMarketWithSymbol(1500, "BTCUSDT_PERP", 10'000.0, 1000.0));
    ASSERT_EQ(s4.size(), 1u);
    EXPECT_DOUBLE_EQ(s4[0].target_notional, 800.0);

    // t=2200 is one full batch interval after last increase (t=1200), so increase can apply.
    const auto s5 = scheduler.BuildSlices(
        parent_orders,
        account,
        signal,
        MakeMarketWithSymbol(2200, "BTCUSDT_PERP", 10'000.0, 1000.0));
    ASSERT_EQ(s5.size(), 1u);
    EXPECT_DOUBLE_EQ(s5[0].target_notional, 1100.0);
}

