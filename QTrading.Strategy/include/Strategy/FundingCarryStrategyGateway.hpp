#pragma once

#include "Dto/Trading/InstrumentSpec.hpp"
#include "Execution/ExecutionOrder.hpp"
#include "Monitoring/MonitoringAlert.hpp"
#include "Risk/AccountState.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace QTrading::Infra::Exchanges::BinanceSim {
class BinanceExchange;
}

namespace QTrading::Strategy {

class FundingCarryStrategyGateway {
public:
    FundingCarryStrategyGateway(
        std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange> exchange,
        std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType> instrument_types);

    QTrading::Risk::AccountState BuildAccountState() const;
    void SubmitOrders(const std::vector<QTrading::Execution::ExecutionOrder>& orders);
    void ApplyMonitoringAlerts(const std::vector<QTrading::Monitoring::MonitoringAlert>& alerts);

private:
    std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange> exchange_;
    std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType> instrument_types_;
};

} // namespace QTrading::Strategy
