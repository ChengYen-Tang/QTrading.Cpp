#pragma once

#include "Dto/Market/Binance/MultiKline.hpp"
#include "Execution/ExecutionOrder.hpp"
#include "Signal/SignalDecision.hpp"

#include <memory>
#include <vector>

namespace QTrading::Execution {

/// @brief Interface for final order coordination across legs/pairs before submission.
class IPairCoordinator {
public:
    virtual ~IPairCoordinator() = default;

    virtual std::vector<ExecutionOrder> Coordinate(
        std::vector<ExecutionOrder> orders,
        const QTrading::Signal::SignalDecision& signal,
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market) = 0;
};

/// @brief Default coordinator: no cross-leg adjustment.
class PassThroughPairCoordinator final : public IPairCoordinator {
public:
    std::vector<ExecutionOrder> Coordinate(
        std::vector<ExecutionOrder> orders,
        const QTrading::Signal::SignalDecision&,
        const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>&) override
    {
        return orders;
    }
};

} // namespace QTrading::Execution
