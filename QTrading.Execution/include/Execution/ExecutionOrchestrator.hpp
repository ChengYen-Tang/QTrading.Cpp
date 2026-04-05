#pragma once

#include "Execution/ExecutionParentOrder.hpp"
#include "Execution/ExecutionSignal.hpp"
#include "Execution/IExecutionEngine.hpp"
#include "Execution/IExecutionPolicy.hpp"
#include "Execution/IExecutionScheduler.hpp"
#include "Risk/AccountState.hpp"
#include "Risk/RiskTarget.hpp"

#include <memory>
#include <vector>

namespace QTrading::Execution {

class ExecutionOrchestrator {
public:
    using MarketPtr = std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>;

    ExecutionOrchestrator(
        IExecutionEngine<MarketPtr>& execution_engine,
        IExecutionScheduler& scheduler,
        IExecutionPolicy& policy);

    std::vector<ExecutionOrder> Execute(
        const QTrading::Risk::RiskTarget& strategy_target,
        const QTrading::Risk::AccountState& account,
        const ExecutionSignal& signal,
        const MarketPtr& market);

    static std::vector<ExecutionParentOrder> BuildParentOrders(
        const QTrading::Risk::RiskTarget& strategy_target);

private:
    IExecutionEngine<MarketPtr>& execution_engine_;
    IExecutionScheduler& scheduler_;
    IExecutionPolicy& policy_;
};

} // namespace QTrading::Execution
