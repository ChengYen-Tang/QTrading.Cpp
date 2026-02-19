#include "Execution/FundingCarryExecutionOrchestrator.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <vector>

using MarketPtr = std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>;

namespace {

MarketPtr MakeMarket(uint64_t ts_ms)
{
    auto market = std::make_shared<QTrading::Dto::Market::Binance::MultiKlineDto>();
    market->Timestamp = ts_ms;
    return market;
}

class RecordingExecutionEngine final : public QTrading::Execution::IExecutionEngine<MarketPtr> {
public:
    std::vector<QTrading::Execution::ExecutionOrder> plan(
        const QTrading::Risk::RiskTarget& target,
        const QTrading::Signal::SignalDecision&,
        const MarketPtr&) override
    {
        last_target = target;
        plan_calls += 1;
        return emitted_orders;
    }

    QTrading::Risk::RiskTarget last_target{};
    int plan_calls = 0;
    std::vector<QTrading::Execution::ExecutionOrder> emitted_orders{};
};

class StaticSliceScheduler final : public QTrading::Execution::IExecutionScheduler {
public:
    std::vector<QTrading::Execution::ExecutionSlice> BuildSlices(
        const std::vector<QTrading::Execution::ExecutionParentOrder>& parent_orders,
        const QTrading::Risk::AccountState& account,
        const QTrading::Signal::SignalDecision&,
        const MarketPtr&) override
    {
        observed_parent_orders = parent_orders;
        observed_account = account;
        return forced_slices;
    }

    std::vector<QTrading::Execution::ExecutionParentOrder> observed_parent_orders{};
    QTrading::Risk::AccountState observed_account{};
    std::vector<QTrading::Execution::ExecutionSlice> forced_slices{};
};

class RecordingPolicy final : public QTrading::Execution::IExecutionPolicy {
public:
    QTrading::Risk::RiskTarget BuildExecutionTarget(
        const std::vector<QTrading::Execution::ExecutionSlice>& slices,
        const QTrading::Risk::RiskTarget& strategy_target) override
    {
        observed_slices = slices;
        observed_strategy_target = strategy_target;
        return forced_target;
    }

    std::vector<QTrading::Execution::ExecutionSlice> observed_slices{};
    QTrading::Risk::RiskTarget observed_strategy_target{};
    QTrading::Risk::RiskTarget forced_target{};
};

class RecordingCoordinator final : public QTrading::Execution::IPairCoordinator {
public:
    std::vector<QTrading::Execution::ExecutionOrder> Coordinate(
        std::vector<QTrading::Execution::ExecutionOrder> orders,
        const QTrading::Signal::SignalDecision&,
        const MarketPtr&) override
    {
        observed_orders = orders;
        if (override_orders.has_value()) {
            return *override_orders;
        }
        return orders;
    }

    std::vector<QTrading::Execution::ExecutionOrder> observed_orders{};
    std::optional<std::vector<QTrading::Execution::ExecutionOrder>> override_orders{};
};

} // namespace

TEST(FundingCarryExecutionOrchestratorTests, BuildParentOrdersUsesRiskTargetAndDefaultLeverage)
{
    QTrading::Risk::RiskTarget target;
    target.ts_ms = 1234;
    target.target_positions["BTCUSDT_SPOT"] = 1'000.0;
    target.target_positions["BTCUSDT_PERP"] = -1'000.0;
    target.leverage["BTCUSDT_PERP"] = 3.0;

    const auto parent_orders =
        QTrading::Execution::FundingCarryExecutionOrchestrator::BuildParentOrders(target);

    ASSERT_EQ(parent_orders.size(), 2u);
    bool seen_spot = false;
    bool seen_perp = false;
    for (const auto& order : parent_orders) {
        EXPECT_EQ(order.ts_ms, 1234u);
        if (order.symbol == "BTCUSDT_SPOT") {
            seen_spot = true;
            EXPECT_DOUBLE_EQ(order.target_notional, 1'000.0);
            EXPECT_DOUBLE_EQ(order.leverage, 1.0);
        }
        if (order.symbol == "BTCUSDT_PERP") {
            seen_perp = true;
            EXPECT_DOUBLE_EQ(order.target_notional, -1'000.0);
            EXPECT_DOUBLE_EQ(order.leverage, 3.0);
        }
    }
    EXPECT_TRUE(seen_spot);
    EXPECT_TRUE(seen_perp);
}

TEST(FundingCarryExecutionOrchestratorTests, ExecuteUsesSchedulerPolicyEngineAndCoordinator)
{
    RecordingExecutionEngine engine;
    StaticSliceScheduler scheduler;
    RecordingPolicy policy;
    RecordingCoordinator coordinator;

    scheduler.forced_slices = {
        QTrading::Execution::ExecutionSlice{
            10,
            "BTCUSDT_PERP",
            -500.0,
            2.0,
        },
    };

    policy.forced_target.ts_ms = 10;
    policy.forced_target.strategy = "funding_carry";
    policy.forced_target.target_positions["BTCUSDT_PERP"] = -500.0;
    policy.forced_target.leverage["BTCUSDT_PERP"] = 2.0;

    QTrading::Execution::ExecutionOrder raw_order;
    raw_order.ts_ms = 10;
    raw_order.symbol = "BTCUSDT_PERP";
    raw_order.action = QTrading::Execution::OrderAction::Sell;
    raw_order.qty = 0.05;
    engine.emitted_orders = { raw_order };

    QTrading::Execution::ExecutionOrder coordinated_order = raw_order;
    coordinated_order.qty = 0.04;
    coordinator.override_orders = std::vector<QTrading::Execution::ExecutionOrder>{ coordinated_order };

    QTrading::Execution::FundingCarryExecutionOrchestrator orchestrator(
        engine,
        scheduler,
        policy,
        coordinator);

    QTrading::Risk::RiskTarget strategy_target;
    strategy_target.ts_ms = 10;
    strategy_target.strategy = "funding_carry";
    strategy_target.target_positions["BTCUSDT_PERP"] = -1'000.0;
    strategy_target.leverage["BTCUSDT_PERP"] = 2.0;
    strategy_target.target_positions["BTCUSDT_SPOT"] = 1'000.0;
    strategy_target.leverage["BTCUSDT_SPOT"] = 1.0;

    QTrading::Signal::SignalDecision signal;
    signal.strategy = "funding_carry";
    QTrading::Risk::AccountState account;

    const auto result = orchestrator.Execute(strategy_target, account, signal, MakeMarket(10));

    ASSERT_EQ(engine.plan_calls, 1);
    ASSERT_EQ(policy.observed_slices.size(), 1u);
    ASSERT_EQ(scheduler.observed_parent_orders.size(), 2u);
    ASSERT_EQ(coordinator.observed_orders.size(), 1u);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_NEAR(result[0].qty, 0.04, 1e-12);
    EXPECT_DOUBLE_EQ(engine.last_target.target_positions["BTCUSDT_PERP"], -500.0);
}
