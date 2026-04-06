#pragma once

#include "Contracts/StrategyIdentity.hpp"

#include <cstdint>
#include <string>

namespace QTrading::Signal {

/// @brief High-level signal status for a strategy.
enum class SignalStatus {
    Inactive,
    Active,
    Cooldown
};

/// @brief Execution urgency derived from signal.
enum class SignalUrgency {
    Low,
    Medium,
    High
};

/// @brief Signal decision output for downstream intent/risk/execution.
struct SignalDecision {
    /// @brief Milliseconds since epoch.
    std::uint64_t ts_ms = 0;
    /// @brief Primary symbol for this signal (e.g., perp symbol).
    std::string symbol;
    /// @brief Strategy identifier.
    std::string strategy;
    /// @brief Typed strategy identity for downstream policy routing.
    QTrading::Contracts::StrategyKind strategy_kind = QTrading::Contracts::StrategyKind::Unknown;
    /// @brief Activation state.
    SignalStatus status = SignalStatus::Inactive;
    /// @brief Confidence score (0..1).
    double confidence = 0.0;
    /// @brief Strategy-specific allocation score for cross-sectional ranking.
    ///        Basis allocator uses net edge; other strategies may leave this at zero.
    double allocator_score = 0.0;
    /// @brief Execution urgency.
    SignalUrgency urgency = SignalUrgency::Low;
};

} // namespace QTrading::Signal
