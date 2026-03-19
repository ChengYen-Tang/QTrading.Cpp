#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Exchanges/BinanceSimulator/Diagnostics/Compare/ReplayCompareTestHarness.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare {

enum class ReplayCompareCiGateMode : uint8_t {
    CompareQuick = 0,
    ComparePackFull = 1,
};

struct ReplayCompareBaselineVersion final {
    std::string dataset_version;
    std::string scenario_pack_version;
    std::string artifact_format_version;
};

struct ReplayCompareGatePolicy final {
    bool fail_on_semantic_mismatch{ true };
    bool allow_unsupported_with_waiver{ true };
    bool allow_fallback_with_waiver{ true };
    bool require_contract_change_extra_review{ true };
    bool require_compare_evidence_for_high_risk{ true };
    bool soft_gate{ false };
};

struct ReplayCompareCiGatePlan final {
    ReplayCompareCiGateMode mode{ ReplayCompareCiGateMode::CompareQuick };
    std::string gate_name;
    ReplayCompareBaselineVersion baseline;
    ReplayCompareGatePolicy policy;
    ReplayCompareTestHarnessOptions options{};
};

struct ReplayCompareCiGateDecision final {
    bool pass{ false };
    bool requires_waiver{ false };
    uint64_t mismatch_count{ 0 };
    size_t failed_scenarios{ 0 };
    size_t unsupported_scenarios{ 0 };
    size_t fallback_scenarios{ 0 };
    std::string reason;
};

class ReplayCompareCiGate final {
public:
    static ReplayCompareCiGatePlan BuildCompareQuickPlan();
    static ReplayCompareCiGatePlan BuildComparePackFullPlan();
    static ReplayCompareCiGateDecision Evaluate(
        const ReplayCompareScenarioPackResult& result,
        const ReplayCompareGatePolicy& policy);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare

