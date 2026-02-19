#pragma once

#include "Dto/Market/Binance/MultiKline.hpp"
#include "Execution/ExecutionParentOrder.hpp"
#include "Risk/AccountState.hpp"
#include "Signal/SignalDecision.hpp"

#include <memory>
#include <vector>

namespace QTrading::Execution {

/// @brief Interface for converting parent orders into time-sliced execution requests.
class IExecutionScheduler {
public:
    virtual ~IExecutionScheduler() = default;

    virtual std::vector<ExecutionSlice> BuildSlices(
        const std::vector<ExecutionParentOrder>& parent_orders,
        const QTrading::Risk::AccountState& account,
        const QTrading::Signal::SignalDecision& signal,
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) = 0;
};

/// @brief Default scheduler that forwards one full-size slice per parent order.
class PassthroughExecutionScheduler final : public IExecutionScheduler {
public:
    std::vector<ExecutionSlice> BuildSlices(
        const std::vector<ExecutionParentOrder>& parent_orders,
        const QTrading::Risk::AccountState&,
        const QTrading::Signal::SignalDecision&,
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>&) override
    {
        std::vector<ExecutionSlice> slices;
        slices.reserve(parent_orders.size());
        for (const auto& parent : parent_orders) {
            slices.push_back(
                ExecutionSlice{
                    parent.ts_ms,
                    parent.symbol,
                    parent.target_notional,
                    parent.leverage,
                });
        }
        return slices;
    }
};

} // namespace QTrading::Execution
