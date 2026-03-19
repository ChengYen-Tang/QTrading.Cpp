#include <gtest/gtest.h>

#include "Exchanges/BinanceSimulator/Diagnostics/Compare/ReplayCompareCiGate.hpp"
#include "Exchanges/BinanceSimulator/Diagnostics/Compare/ReplayCompareTestHarness.hpp"
#include "Exchanges/BinanceSimulator/Diagnostics/Compare/V2ReplayScenarioPack.hpp"

namespace ReplayCompare = QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare;

TEST(ReplayCompareCiGateTests, CompareQuickPlanPinsBaselineAndUsesSummaryArtifacts)
{
    const auto plan = ReplayCompare::ReplayCompareCiGate::BuildCompareQuickPlan();
    EXPECT_EQ(plan.gate_name, "compare-quick");
    EXPECT_EQ(plan.baseline.dataset_version, "v2-legacy-replay-pack-20260319");
    EXPECT_EQ(plan.baseline.scenario_pack_version, "milestone5-v2-session-replay-pack-20260319");
    EXPECT_EQ(plan.baseline.artifact_format_version, "replay-compare-artifact-v1");
    EXPECT_EQ(plan.options.execution_mode, ReplayCompare::ReplayCompareExecutionMode::ProductionSafe);
    EXPECT_TRUE(plan.options.generate_artifact);
    EXPECT_EQ(plan.options.artifact_mode, ReplayCompare::DiagnosticReportMode::Summary);
    EXPECT_FALSE(plan.options.scenario_name_filters.empty());
}

TEST(ReplayCompareCiGateTests, ComparePackFullPlanRunsAllScenariosWithDetailedArtifacts)
{
    const auto plan = ReplayCompare::ReplayCompareCiGate::BuildComparePackFullPlan();
    EXPECT_EQ(plan.gate_name, "compare-pack-full");
    EXPECT_EQ(plan.options.execution_mode, ReplayCompare::ReplayCompareExecutionMode::TestValidation);
    EXPECT_TRUE(plan.options.generate_artifact);
    EXPECT_EQ(plan.options.artifact_mode, ReplayCompare::DiagnosticReportMode::Detailed);
    EXPECT_TRUE(plan.options.scenario_name_filters.empty());
}

TEST(ReplayCompareCiGateTests, CompareQuickAndFullPlansRunViaHarness)
{
    const auto scenarios = ReplayCompare::V2ReplayScenarioPack::BuildCoreScenarioPack();
    ReplayCompare::ReplayCompareTestHarness harness;

    const auto quick_plan = ReplayCompare::ReplayCompareCiGate::BuildCompareQuickPlan();
    const auto quick_result = harness.RunScenarioPack(scenarios, quick_plan.options);
    const auto quick_decision = ReplayCompare::ReplayCompareCiGate::Evaluate(quick_result, quick_plan.policy);
    EXPECT_TRUE(quick_decision.pass);
    EXPECT_EQ(quick_decision.mismatch_count, 0u);

    const auto full_plan = ReplayCompare::ReplayCompareCiGate::BuildComparePackFullPlan();
    const auto full_result = harness.RunScenarioPack(scenarios, full_plan.options);
    const auto full_decision = ReplayCompare::ReplayCompareCiGate::Evaluate(full_result, full_plan.policy);
    EXPECT_TRUE(full_decision.pass);
    EXPECT_EQ(full_decision.mismatch_count, 0u);
}

TEST(ReplayCompareCiGateTests, GatePolicyFailsOnSemanticMismatch)
{
    ReplayCompare::ReplayCompareScenarioPackResult result{};
    result.total_mismatch_count = 3;
    result.failed_scenarios = 1;

    ReplayCompare::ReplayCompareGatePolicy policy{};
    policy.fail_on_semantic_mismatch = true;
    policy.allow_unsupported_with_waiver = true;
    policy.allow_fallback_with_waiver = true;
    policy.soft_gate = false;

    const auto decision = ReplayCompare::ReplayCompareCiGate::Evaluate(result, policy);
    EXPECT_FALSE(decision.pass);
    EXPECT_FALSE(decision.requires_waiver);
    EXPECT_EQ(decision.reason, "semantic mismatch detected");
}

TEST(ReplayCompareCiGateTests, UnsupportedFallbackNeedsWaiverWhenAllowed)
{
    ReplayCompare::ReplayCompareScenarioPackResult result{};
    result.total_mismatch_count = 0;
    result.failed_scenarios = 0;
    result.unsupported_scenarios = 2;
    result.fallback_scenarios = 1;
    result.all_passed = false;

    ReplayCompare::ReplayCompareGatePolicy policy{};
    policy.fail_on_semantic_mismatch = true;
    policy.allow_unsupported_with_waiver = true;
    policy.allow_fallback_with_waiver = true;
    policy.soft_gate = false;

    const auto decision = ReplayCompare::ReplayCompareCiGate::Evaluate(result, policy);
    EXPECT_TRUE(decision.pass);
    EXPECT_TRUE(decision.requires_waiver);
}

TEST(ReplayCompareCiGateTests, SoftGateConvertsSemanticMismatchToWarningWithWaiver)
{
    ReplayCompare::ReplayCompareScenarioPackResult result{};
    result.total_mismatch_count = 2;
    result.failed_scenarios = 1;

    ReplayCompare::ReplayCompareGatePolicy policy{};
    policy.fail_on_semantic_mismatch = true;
    policy.allow_unsupported_with_waiver = true;
    policy.allow_fallback_with_waiver = true;
    policy.soft_gate = true;

    const auto decision = ReplayCompare::ReplayCompareCiGate::Evaluate(result, policy);
    EXPECT_TRUE(decision.pass);
    EXPECT_TRUE(decision.requires_waiver);
    EXPECT_EQ(decision.reason, "soft gate warning: semantic mismatch detected");
}
