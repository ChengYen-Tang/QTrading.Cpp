#pragma once

#include <cstdint>
#include <string>

namespace QTrading::Execution {

/// @brief Strategy-level parent order representing desired target notional for one symbol.
struct ExecutionParentOrder {
    /// @brief Milliseconds since epoch.
    std::uint64_t ts_ms = 0;
    /// @brief Trading symbol.
    std::string symbol;
    /// @brief Desired total target notional for this symbol.
    double target_notional = 0.0;
    /// @brief Leverage hint for this symbol.
    double leverage = 1.0;
};

/// @brief Scheduled execution slice derived from a parent order.
struct ExecutionSlice {
    /// @brief Milliseconds since epoch.
    std::uint64_t ts_ms = 0;
    /// @brief Trading symbol.
    std::string symbol;
    /// @brief Target notional forwarded to execution for this slice.
    double target_notional = 0.0;
    /// @brief Leverage hint for this symbol.
    double leverage = 1.0;
};

} // namespace QTrading::Execution

