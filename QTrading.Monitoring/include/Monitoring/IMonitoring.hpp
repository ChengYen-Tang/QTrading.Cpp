#pragma once

#include <vector>
#include "MonitoringAlert.hpp"
#include "Risk/AccountState.hpp"

namespace QTrading::Monitoring {

/// @brief Interface for post-trade risk monitoring.
class IMonitoring {
public:
    /// @brief Virtual destructor.
    virtual ~IMonitoring() = default;
    /// @brief Evaluate monitoring alerts for current account state.
    virtual std::vector<MonitoringAlert> check(const QTrading::Risk::AccountState& account) = 0;
};

/// @brief No-op monitoring returning no alerts.
class NullMonitoring final : public IMonitoring {
public:
    std::vector<MonitoringAlert> check(const QTrading::Risk::AccountState&) override { return {}; }
};

} // namespace QTrading::Monitoring
