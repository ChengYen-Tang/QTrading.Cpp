#pragma once

#include "Execution/ExecutionParentOrder.hpp"
#include "Execution/IExecutionEngine.hpp"
#include "Execution/IExecutionPolicy.hpp"
#include "Execution/IExecutionScheduler.hpp"
#include "Risk/AccountState.hpp"
#include "Risk/RiskTarget.hpp"
#include "Signal/SignalDecision.hpp"

#include <memory>
#include <vector>

namespace QTrading::Execution {

/// @brief Orchestrates parent-order workflow: parent -> scheduler -> policy -> engine.
class FundingCarryExecutionOrchestrator {
public:
    using MarketPtr = std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>;

    FundingCarryExecutionOrchestrator(
        IExecutionEngine<MarketPtr>& execution_engine,
        IExecutionScheduler& scheduler,
        IExecutionPolicy& policy);

    virtual std::vector<ExecutionOrder> Execute(
        const QTrading::Risk::RiskTarget& strategy_target,
        const QTrading::Risk::AccountState& account,
        const QTrading::Signal::SignalDecision& signal,
        const MarketPtr& market);

    static std::vector<ExecutionParentOrder> BuildParentOrders(
        const QTrading::Risk::RiskTarget& strategy_target);

private:
    IExecutionEngine<MarketPtr>& execution_engine_;
    IExecutionScheduler& scheduler_;
    IExecutionPolicy& policy_;
};

} // namespace QTrading::Execution
