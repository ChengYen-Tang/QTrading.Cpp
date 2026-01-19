#pragma once

#include <cstddef>
#include <string>
#include "IMonitoring.hpp"

namespace QTrading::Monitoring {

/// @brief Basic monitoring with simple order-count guardrails.
class SimpleMonitoring final : public IMonitoring {
public:
    struct Config {
        std::size_t max_open_orders_per_symbol = 5;
    };

    explicit SimpleMonitoring(Config cfg);

    std::vector<MonitoringAlert> check(const QTrading::Risk::AccountState& account) override;

private:
    Config cfg_;
};

} // namespace QTrading::Monitoring
