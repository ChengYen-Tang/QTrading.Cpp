#include "Monitoring/SimpleMonitoring.hpp"

#include <unordered_map>

namespace QTrading::Monitoring {

SimpleMonitoring::SimpleMonitoring(Config cfg)
    : cfg_(cfg)
{
}

std::vector<MonitoringAlert> SimpleMonitoring::check(const QTrading::Risk::AccountState& account)
{
    std::vector<MonitoringAlert> alerts;
    std::unordered_map<std::string, std::size_t> counts;
    for (const auto& ord : account.open_orders) {
        counts[ord.symbol] += 1;
    }
    for (const auto& kv : counts) {
        if (kv.second <= cfg_.max_open_orders_per_symbol) {
            continue;
        }
        MonitoringAlert alert;
        alert.level = AlertLevel::Warn;
        alert.symbol = kv.first;
        alert.reason = "OPEN_ORDERS_EXCEEDED";
        alert.action = "CANCEL_OPEN_ORDERS";
        alerts.push_back(std::move(alert));
    }
    return alerts;
}

} // namespace QTrading::Monitoring
