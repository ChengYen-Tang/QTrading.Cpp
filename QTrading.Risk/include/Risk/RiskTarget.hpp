#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace QTrading::Risk {

/// @brief Risk sizing output describing target exposures.
struct RiskTarget {
    /// @brief Milliseconds since epoch.
    std::uint64_t ts_ms = 0;
    /// @brief Strategy identifier.
    std::string strategy;
    /// @brief Target position per instrument.
    std::unordered_map<std::string, double> target_positions;
    /// @brief Leverage per instrument.
    std::unordered_map<std::string, double> leverage;
    /// @brief Maximum leverage constraint.
    double max_leverage = 0.0;
    /// @brief Fraction of risk budget used.
    double risk_budget_used = 0.0;
    /// @brief Block opening new positions when true.
    bool block_new_entries = false;
    /// @brief Force risk reduction when true.
    bool force_reduce = false;
};

} // namespace QTrading::Risk
