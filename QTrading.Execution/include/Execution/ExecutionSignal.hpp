#pragma once

#include <cstdint>
#include <string>

namespace QTrading::Execution {

enum class ExecutionSignalStatus {
    Inactive,
    Active,
    Cooldown
};

enum class ExecutionUrgency {
    Low,
    Medium,
    High
};

struct ExecutionSignal {
    std::uint64_t ts_ms = 0;
    std::string symbol;
    std::string strategy;
    ExecutionSignalStatus status = ExecutionSignalStatus::Inactive;
    double confidence = 0.0;
    ExecutionUrgency urgency = ExecutionUrgency::Low;
};

} // namespace QTrading::Execution
