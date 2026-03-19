#include "Exchanges/BinanceSimulator/Diagnostics/Compare/ReplayCompareCiGate.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare {

namespace {

ReplayCompareBaselineVersion BuildPinnedBaselineVersion()
{
    ReplayCompareBaselineVersion out{};
    out.dataset_version = "v2-legacy-replay-pack-20260319";
    out.scenario_pack_version = "milestone5-v2-session-replay-pack-20260319";
    out.artifact_format_version = "replay-compare-artifact-v1";
    return out;
}

ReplayCompareGatePolicy BuildDefaultGatePolicy()
{
    ReplayCompareGatePolicy policy{};
    policy.fail_on_semantic_mismatch = true;
    policy.allow_unsupported_with_waiver = true;
    policy.allow_fallback_with_waiver = true;
    policy.require_contract_change_extra_review = true;
    policy.require_compare_evidence_for_high_risk = true;
    policy.soft_gate = false;
    return policy;
}

} // namespace

ReplayCompareCiGatePlan ReplayCompareCiGate::BuildCompareQuickPlan()
{
    ReplayCompareCiGatePlan plan{};
    plan.mode = ReplayCompareCiGateMode::CompareQuick;
    plan.gate_name = "compare-quick";
    plan.baseline = BuildPinnedBaselineVersion();
    plan.policy = BuildDefaultGatePolicy();

    plan.options.execution_mode = ReplayCompareExecutionMode::ProductionSafe;
    plan.options.feature_set = ReplayCompareFeatureSet::All();
    plan.options.generate_artifact = true;
    plan.options.artifact_mode = DiagnosticReportMode::Summary;
    plan.options.human_readable_mode = DiagnosticReportMode::Summary;
    plan.options.include_legacy_row_snapshot = false;
    plan.options.align_step_boundary_by_common_payload = true;
    plan.options.scenario_name_filters = {
        "v2-vs-legacy.single-symbol",
        "v2-vs-legacy.funding-reference-edge",
        "v2-vs-legacy.async-ack-latency",
    };
    plan.options.run_id_override = 8001;
    plan.options.random_seed = 0xA11CEu;
    return plan;
}

ReplayCompareCiGatePlan ReplayCompareCiGate::BuildComparePackFullPlan()
{
    ReplayCompareCiGatePlan plan{};
    plan.mode = ReplayCompareCiGateMode::ComparePackFull;
    plan.gate_name = "compare-pack-full";
    plan.baseline = BuildPinnedBaselineVersion();
    plan.policy = BuildDefaultGatePolicy();

    plan.options.execution_mode = ReplayCompareExecutionMode::TestValidation;
    plan.options.feature_set = ReplayCompareFeatureSet::All();
    plan.options.generate_artifact = true;
    plan.options.artifact_mode = DiagnosticReportMode::Detailed;
    plan.options.human_readable_mode = DiagnosticReportMode::Detailed;
    plan.options.include_legacy_row_snapshot = true;
    plan.options.align_step_boundary_by_common_payload = true;
    plan.options.scenario_name_filters.clear();
    plan.options.run_id_override = 9001;
    plan.options.random_seed = 0xBEEF12u;
    return plan;
}

ReplayCompareCiGateDecision ReplayCompareCiGate::Evaluate(
    const ReplayCompareScenarioPackResult& result,
    const ReplayCompareGatePolicy& policy)
{
    ReplayCompareCiGateDecision out{};
    out.mismatch_count = result.total_mismatch_count;
    out.failed_scenarios = result.failed_scenarios;
    out.unsupported_scenarios = result.unsupported_scenarios;
    out.fallback_scenarios = result.fallback_scenarios;

    const bool semantic_mismatch = (result.failed_scenarios > 0) || (result.total_mismatch_count > 0);
    const bool unsupported_present = result.unsupported_scenarios > 0;
    const bool fallback_present = result.fallback_scenarios > 0;

    bool pass = true;
    std::string reason = "gate passed";

    if (policy.fail_on_semantic_mismatch && semantic_mismatch) {
        pass = false;
        reason = "semantic mismatch detected";
    }
    if (!policy.allow_unsupported_with_waiver && unsupported_present) {
        pass = false;
        reason = "unsupported scenario present without waiver policy";
    }
    if (!policy.allow_fallback_with_waiver && fallback_present) {
        pass = false;
        reason = "fallback scenario present without waiver policy";
    }

    out.requires_waiver =
        (unsupported_present && policy.allow_unsupported_with_waiver) ||
        (fallback_present && policy.allow_fallback_with_waiver);

    if (!pass && policy.soft_gate) {
        out.pass = true;
        out.requires_waiver = true;
        out.reason = "soft gate warning: " + reason;
        return out;
    }

    out.pass = pass;
    out.reason = reason;
    return out;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare
