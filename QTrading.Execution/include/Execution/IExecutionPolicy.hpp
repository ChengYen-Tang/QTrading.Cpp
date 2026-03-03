#pragma once

#include "Execution/ExecutionParentOrder.hpp"
#include "Risk/RiskTarget.hpp"

#include <vector>

namespace QTrading::Execution {

/// @brief Interface for translating scheduled slices into a concrete RiskTarget for execution.
class IExecutionPolicy {
public:
    virtual ~IExecutionPolicy() = default;

    virtual QTrading::Risk::RiskTarget BuildExecutionTarget(
        const std::vector<ExecutionSlice>& slices,
        const QTrading::Risk::RiskTarget& strategy_target) = 0;
};

/// @brief Default policy: use slice notionals directly as execution target notionals.
class TargetNotionalExecutionPolicy final : public IExecutionPolicy {
public:
    QTrading::Risk::RiskTarget BuildExecutionTarget(
        const std::vector<ExecutionSlice>& slices,
        const QTrading::Risk::RiskTarget& strategy_target) override
    {
        QTrading::Risk::RiskTarget execution_target = strategy_target;
        execution_target.target_positions.clear();
        execution_target.leverage.clear();

        execution_target.target_positions.reserve(slices.size());
        execution_target.leverage.reserve(slices.size());
        for (const auto& slice : slices) {
            execution_target.target_positions[slice.symbol] = slice.target_notional;
            execution_target.leverage[slice.symbol] = slice.leverage;
        }
        return execution_target;
    }
};

} // namespace QTrading::Execution

