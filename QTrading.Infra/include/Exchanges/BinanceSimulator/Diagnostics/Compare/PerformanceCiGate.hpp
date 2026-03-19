#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Exchanges/BinanceSimulator/Diagnostics/Compare/ReplayCompareCiGate.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare {

enum class PerformanceGateComparison {
    LessOrEqual = 0,
    GreaterOrEqual = 1,
};

struct PerformanceGateMetricCheck final {
    std::string gate_name;
    std::string metric_name;
    std::string scenario_name;
    double actual{ 0.0 };
    double budget{ 0.0 };
    PerformanceGateComparison comparison{ PerformanceGateComparison::LessOrEqual };
};

struct PerformanceGatePolicy final {
    bool fail_fast{ true };
    bool require_replay_compare_gate{ true };
};

struct PerformanceGateDecision final {
    bool pass{ true };
    bool requires_waiver{ false };
    std::optional<std::string> first_failing_metric{};
    std::vector<std::string> failure_lines{};
};

class PerformanceCiGate final {
public:
    static PerformanceGateDecision Evaluate(
        const std::vector<PerformanceGateMetricCheck>& checks,
        const PerformanceGatePolicy& policy);

    static PerformanceGateDecision EvaluateWithReplayCompare(
        const std::vector<PerformanceGateMetricCheck>& checks,
        const PerformanceGatePolicy& policy,
        const ReplayCompareCiGateDecision& replay_compare_decision);

    static std::string FormatFailLine(const PerformanceGateMetricCheck& check);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare
