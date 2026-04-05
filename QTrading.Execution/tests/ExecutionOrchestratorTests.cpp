#include "Execution/ExecutionOrchestrator.hpp"

#include "gtest/gtest.h"

namespace {

using MarketPtr = std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>;

class StubExecutionEngine final : public QTrading::Execution::IExecutionEngine<MarketPtr> {
public:
    std::vector<QTrading::Execution::ExecutionOrder> plan(
        const QTrading::Risk::RiskTarget& target,
        const QTrading::Execution::ExecutionSignal&,
        const MarketPtr&) override
    {
        captured_target = target;
        return result;
    }

    QTrading::Risk::RiskTarget captured_target{};
    std::vector<QTrading::Execution::ExecutionOrder> result{};
};

class StubExecutionScheduler final : public QTrading::Execution::IExecutionScheduler {
public:
    std::vector<QTrading::Execution::ExecutionSlice> BuildSlices(
        const std::vector<QTrading::Execution::ExecutionParentOrder>& parent_orders,
        const QTrading::Risk::AccountState&,
        const QTrading::Execution::ExecutionSignal&,
        const MarketPtr&) override
    {
        captured_parent_orders = parent_orders;
        return slices;
    }

    std::vector<QTrading::Execution::ExecutionParentOrder> captured_parent_orders{};
    std::vector<QTrading::Execution::ExecutionSlice> slices{};
};

class StubExecutionPolicy final : public QTrading::Execution::IExecutionPolicy {
public:
    QTrading::Risk::RiskTarget BuildExecutionTarget(
        const std::vector<QTrading::Execution::ExecutionSlice>& slices,
        const QTrading::Risk::RiskTarget& strategy_target) override
    {
        captured_slices = slices;
        captured_strategy_target = strategy_target;
        return result;
    }

    std::vector<QTrading::Execution::ExecutionSlice> captured_slices{};
    QTrading::Risk::RiskTarget captured_strategy_target{};
    QTrading::Risk::RiskTarget result{};
};

} // namespace

TEST(ExecutionOrchestratorTests, BuildParentOrdersUsesRiskTargetAndDefaultLeverage)
{
    QTrading::Risk::RiskTarget target;
    target.ts_ms = 1234;
    target.target_positions["BTCUSDT_PERP"] = 1000.0;
    target.target_positions["ETHUSDT_PERP"] = -500.0;
    target.leverage["BTCUSDT_PERP"] = 3.0;

    const auto parents =
        QTrading::Execution::ExecutionOrchestrator::BuildParentOrders(target);

    ASSERT_EQ(parents.size(), 2u);
    EXPECT_EQ(parents[0].ts_ms, 1234u);
    EXPECT_EQ(parents[0].symbol, "BTCUSDT_PERP");
    EXPECT_DOUBLE_EQ(parents[0].target_notional, 1000.0);
    EXPECT_DOUBLE_EQ(parents[0].leverage, 3.0);
    EXPECT_EQ(parents[1].symbol, "ETHUSDT_PERP");
    EXPECT_DOUBLE_EQ(parents[1].target_notional, -500.0);
    EXPECT_DOUBLE_EQ(parents[1].leverage, 1.0);
}

TEST(ExecutionOrchestratorTests, ExecuteUsesSchedulerPolicyAndEngine)
{
    StubExecutionEngine engine;
    StubExecutionScheduler scheduler;
    StubExecutionPolicy policy;

    scheduler.slices.push_back({ 100u, "BTCUSDT_PERP", 500.0, 2.0 });
    policy.result.ts_ms = 100u;
    policy.result.target_positions["BTCUSDT_PERP"] = 500.0;
    engine.result.push_back(
        { 100u, "BTCUSDT_PERP", QTrading::Execution::OrderAction::Buy, 1.0, 0.0, QTrading::Execution::OrderType::Market, false, QTrading::Execution::OrderUrgency::Low });

    QTrading::Execution::ExecutionOrchestrator orchestrator(engine, scheduler, policy);

    QTrading::Risk::RiskTarget target;
    target.ts_ms = 100u;
    target.target_positions["BTCUSDT_PERP"] = 700.0;
    target.leverage["BTCUSDT_PERP"] = 2.0;

    QTrading::Risk::AccountState account;
    QTrading::Execution::ExecutionSignal signal;
    signal.strategy = "funding_carry";

    auto market = std::make_shared<QTrading::Dto::Market::Binance::MultiKlineDto>();

    const auto orders = orchestrator.Execute(target, account, signal, market);

    ASSERT_EQ(scheduler.captured_parent_orders.size(), 1u);
    EXPECT_EQ(scheduler.captured_parent_orders[0].symbol, "BTCUSDT_PERP");
    ASSERT_EQ(policy.captured_slices.size(), 1u);
    EXPECT_EQ(policy.captured_slices[0].symbol, "BTCUSDT_PERP");
    EXPECT_EQ(engine.captured_target.target_positions["BTCUSDT_PERP"], 500.0);
    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(orders[0].symbol, "BTCUSDT_PERP");
}
