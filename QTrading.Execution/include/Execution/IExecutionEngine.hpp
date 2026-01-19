#pragma once

#include <vector>
#include "ExecutionOrder.hpp"
#include "Risk/RiskTarget.hpp"
#include "Signal/SignalDecision.hpp"

namespace QTrading::Execution {

/// @brief Interface for converting risk targets into concrete orders.
template <typename TMarket>
class IExecutionEngine {
public:
    /// @brief Virtual destructor.
    virtual ~IExecutionEngine() = default;
    /// @brief Create execution orders based on risk, signal, and market data.
    virtual std::vector<ExecutionOrder> plan(
        const QTrading::Risk::RiskTarget& target,
        const QTrading::Signal::SignalDecision& signal,
        const TMarket& market) = 0;
};

/// @brief No-op execution engine returning no orders.
template <typename TMarket>
class NullExecutionEngine final : public IExecutionEngine<TMarket> {
public:
    std::vector<ExecutionOrder> plan(
        const QTrading::Risk::RiskTarget&, const QTrading::Signal::SignalDecision&, const TMarket&) override {
        return {};
    }
};

} // namespace QTrading::Execution
