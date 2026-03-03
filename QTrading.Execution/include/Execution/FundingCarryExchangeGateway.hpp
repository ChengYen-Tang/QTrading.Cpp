#pragma once

#include "Execution/ExecutionOrder.hpp"
#include "Monitoring/MonitoringAlert.hpp"
#include "Risk/AccountState.hpp"
#include "Dto/Trading/InstrumentSpec.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace QTrading::Infra::Exchanges::BinanceSim {
class BinanceExchange;
}

namespace QTrading::Execution {

/// @brief Exchange-facing adapter for funding-carry strategy.
/// Encapsulates account snapshot + order/alert side effects.
class FundingCarryExchangeGateway {
public:
    FundingCarryExchangeGateway(
        std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange> exchange,
        std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType> instrument_types);

    virtual QTrading::Risk::AccountState BuildAccountState() const;
    virtual void SubmitOrders(const std::vector<QTrading::Execution::ExecutionOrder>& orders);
    virtual void ApplyMonitoringAlerts(const std::vector<QTrading::Monitoring::MonitoringAlert>& alerts);

private:
    std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange> exchange_;
    std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType> instrument_types_;
};

} // namespace QTrading::Execution

