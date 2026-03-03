#pragma once

#include <cstdint>
#include <string>

namespace QTrading::Monitoring {

/// @brief Severity level for monitoring alerts.
enum class AlertLevel {
    Info,
    Warn,
    Critical
};

/// @brief Alert produced by monitoring checks.
struct MonitoringAlert {
    /// @brief Milliseconds since epoch.
    std::uint64_t ts_ms = 0;
    /// @brief Symbol associated with the alert (optional).
    std::string symbol;
    /// @brief Alert severity.
    AlertLevel level = AlertLevel::Info;
    /// @brief Reason code or description.
    std::string reason;
    /// @brief Suggested action (e.g., FORCE_REDUCE).
    std::string action;
};

} // namespace QTrading::Monitoring
