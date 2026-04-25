#include "Execution/ExecutionOrchestrator.hpp"

namespace QTrading::Execution {

ExecutionOrchestrator::ExecutionOrchestrator(
    IExecutionEngine<MarketPtr>& execution_engine,
    IExecutionScheduler& scheduler,
    IExecutionPolicy& policy)
    : execution_engine_(execution_engine)
    , scheduler_(scheduler)
    , policy_(policy)
{
}

std::vector<ExecutionOrder> ExecutionOrchestrator::Execute(
    const QTrading::Risk::RiskTarget& strategy_target,
    const QTrading::Risk::AccountState& account,
    const ExecutionSignal& signal,
    const MarketPtr& market)
{
    if (!market) {
        return {};
    }

    const auto parent_orders = BuildParentOrders(strategy_target);
    const auto slices = scheduler_.BuildSlices(parent_orders, account, signal, market);
    const auto execution_target = policy_.BuildExecutionTarget(slices, strategy_target);
    return execution_engine_.plan(execution_target, signal, market);
}

std::vector<ExecutionParentOrder> ExecutionOrchestrator::BuildParentOrders(
    const QTrading::Risk::RiskTarget& strategy_target)
{
    std::vector<ExecutionParentOrder> parent_orders;
    parent_orders.reserve(strategy_target.target_positions.size());
    for (const auto& [symbol, target_notional] : strategy_target.target_positions) {
        double leverage = 1.0;
        const auto lev_it = strategy_target.leverage.find(symbol);
        if (lev_it != strategy_target.leverage.end()) {
            leverage = lev_it->second;
        }
        parent_orders.push_back(
            ExecutionParentOrder{
                strategy_target.ts_ms,
                symbol,
                target_notional,
                leverage,
            });
    }
    return parent_orders;
}

} // namespace QTrading::Execution
