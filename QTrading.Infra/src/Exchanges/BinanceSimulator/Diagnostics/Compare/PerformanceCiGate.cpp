#include "Exchanges/BinanceSimulator/Diagnostics/Compare/PerformanceCiGate.hpp"

#include <cmath>
#include <sstream>

namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare {

namespace {

bool IsFail(const PerformanceGateMetricCheck& check)
{
    if (!std::isfinite(check.actual) || !std::isfinite(check.budget)) {
        return true;
    }

    if (check.comparison == PerformanceGateComparison::LessOrEqual) {
        return check.actual > check.budget;
    }
    return check.actual < check.budget;
}

} // namespace

std::string PerformanceCiGate::FormatFailLine(const PerformanceGateMetricCheck& check)
{
    std::ostringstream oss;
    oss << "PERF_GATE_FAIL"
        << " gate=" << check.gate_name
        << " metric=" << check.metric_name
        << " scenario=" << check.scenario_name
        << " actual=" << check.actual
        << " budget=" << check.budget
        << " comparison=" << (check.comparison == PerformanceGateComparison::LessOrEqual ? "<=" : ">=");
    return oss.str();
}

PerformanceGateDecision PerformanceCiGate::Evaluate(
    const std::vector<PerformanceGateMetricCheck>& checks,
    const PerformanceGatePolicy& policy)
{
    PerformanceGateDecision out{};
    out.pass = true;
    out.requires_waiver = false;

    for (const auto& check : checks) {
        if (!IsFail(check)) {
            continue;
        }

        out.pass = false;
        out.failure_lines.push_back(FormatFailLine(check));
        if (!out.first_failing_metric.has_value()) {
            out.first_failing_metric = check.metric_name;
        }
        if (policy.fail_fast) {
            break;
        }
    }

    return out;
}

PerformanceGateDecision PerformanceCiGate::EvaluateWithReplayCompare(
    const std::vector<PerformanceGateMetricCheck>& checks,
    const PerformanceGatePolicy& policy,
    const ReplayCompareCiGateDecision& replay_compare_decision)
{
    auto out = Evaluate(checks, policy);
    if (!policy.require_replay_compare_gate) {
        return out;
    }

    if (!replay_compare_decision.pass) {
        out.pass = false;
        out.requires_waiver = false;
        out.failure_lines.push_back("PERF_GATE_FAIL gate=replay-compare reason=" + replay_compare_decision.reason);
        if (!out.first_failing_metric.has_value()) {
            out.first_failing_metric = std::string("replay_compare_gate");
        }
        return out;
    }

    if (replay_compare_decision.requires_waiver) {
        out.requires_waiver = true;
    }
    return out;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare
