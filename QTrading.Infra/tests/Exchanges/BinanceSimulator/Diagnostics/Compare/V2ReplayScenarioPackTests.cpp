#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "Exchanges/BinanceSimulator/Diagnostics/Compare/ReplayCompareTestHarness.hpp"
#include "Exchanges/BinanceSimulator/Diagnostics/Compare/V2ReplayScenarioPack.hpp"

namespace ReplayCompare = QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare;

namespace {

ReplayCompare::ReplayCompareScenarioData* FindScenario(
    std::vector<ReplayCompare::ReplayCompareScenarioData>& scenarios,
    const std::string& name)
{
    for (auto& scenario : scenarios) {
        if (scenario.scenario.name == name) {
            return &scenario;
        }
    }
    return nullptr;
}

const ReplayCompare::ReplayCompareScenarioData* FindScenario(
    const std::vector<ReplayCompare::ReplayCompareScenarioData>& scenarios,
    const std::string& name)
{
    for (const auto& scenario : scenarios) {
        if (scenario.scenario.name == name) {
            return &scenario;
        }
    }
    return nullptr;
}

} // namespace

TEST(V2ReplayScenarioPackTests, CorePackContainsMilestone5RequiredScenarios)
{
    const auto scenarios = ReplayCompare::V2ReplayScenarioPack::BuildCoreScenarioPack();
    ASSERT_EQ(scenarios.size(), 6u);

    std::set<std::string> names;
    bool has_fill = false;
    bool has_funding = false;
    bool has_async_ack = false;
    bool has_rejection = false;
    bool has_liquidation = false;
    for (const auto& scenario : scenarios) {
        names.insert(scenario.scenario.name);
        EXPECT_FALSE(scenario.legacy_steps.empty());
        EXPECT_EQ(scenario.legacy_steps.size(), scenario.candidate_steps.size());
        EXPECT_EQ(scenario.legacy_rows.size(), scenario.candidate_rows.size());
        for (const auto& step : scenario.legacy_steps) {
            for (const auto& event : step.event.events) {
                has_fill = has_fill || event.type == ReplayCompare::ReplayEventType::Fill;
                has_funding = has_funding || event.type == ReplayCompare::ReplayEventType::Funding;
                has_async_ack = has_async_ack || event.type == ReplayCompare::ReplayEventType::AsyncAck;
                has_rejection = has_rejection || event.type == ReplayCompare::ReplayEventType::Rejection;
                has_liquidation = has_liquidation || event.type == ReplayCompare::ReplayEventType::Liquidation;
            }
        }
    }

    EXPECT_EQ(names.count("v2-vs-legacy.single-symbol"), 1u);
    EXPECT_EQ(names.count("v2-vs-legacy.basis-stress"), 1u);
    EXPECT_EQ(names.count("v2-vs-legacy.mixed-spot-perp"), 1u);
    EXPECT_EQ(names.count("v2-vs-legacy.funding-reference-edge"), 1u);
    EXPECT_EQ(names.count("v2-vs-legacy.async-ack-latency"), 1u);
    EXPECT_EQ(names.count("v2-vs-legacy.rejection-liquidation"), 1u);
    EXPECT_TRUE(has_fill);
    EXPECT_TRUE(has_funding);
    EXPECT_TRUE(has_async_ack);
    EXPECT_TRUE(has_rejection);
    EXPECT_TRUE(has_liquidation);
}

TEST(V2ReplayScenarioPackTests, CorePackRunsAllCompareDimensionsForV2VsLegacy)
{
    const auto scenarios = ReplayCompare::V2ReplayScenarioPack::BuildCoreScenarioPack();
    ReplayCompare::ReplayCompareTestHarness harness;

    ReplayCompare::ReplayCompareTestHarnessOptions options{};
    options.execution_mode = ReplayCompare::ReplayCompareExecutionMode::TestValidation;
    options.feature_set = ReplayCompare::ReplayCompareFeatureSet::All();
    options.generate_artifact = true;
    options.include_legacy_row_snapshot = true;

    const auto pack_result = harness.RunScenarioPack(scenarios, options);
    ASSERT_EQ(pack_result.scenario_results.size(), 6u);
    EXPECT_TRUE(pack_result.all_passed);
    EXPECT_EQ(pack_result.failed_scenarios, 0u);
    EXPECT_EQ(pack_result.unsupported_scenarios, 0u);
    EXPECT_EQ(pack_result.fallback_scenarios, 0u);
    EXPECT_EQ(pack_result.total_mismatch_count, 0u);

    for (const auto& scenario_result : pack_result.scenario_results) {
        EXPECT_EQ(scenario_result.report.status, ReplayCompare::ReplayCompareStatus::Success);
        EXPECT_FALSE(scenario_result.report.first_mismatch.has_value());
        EXPECT_FALSE(scenario_result.report.first_divergent_status.has_value());
        EXPECT_TRUE(scenario_result.report.compared_steps > 0);
        ASSERT_TRUE(scenario_result.artifact_json.has_value());
        EXPECT_NE(scenario_result.artifact_json->find("\"first_divergence\""), std::string::npos);
    }
}

TEST(V2ReplayScenarioPackTests, MismatchLocalizationKeepsFirstDivergentStepEventAndRow)
{
    auto scenarios = ReplayCompare::V2ReplayScenarioPack::BuildCoreScenarioPack();
    auto* scenario = FindScenario(scenarios, "v2-vs-legacy.async-ack-latency");
    ASSERT_TRUE(scenario != nullptr);

    ASSERT_GE(scenario->candidate_steps.size(), 2u);
    ASSERT_GE(scenario->candidate_steps[1].event.events.size(), 1u);
    scenario->candidate_steps[1].event.events[0].resolved_step = 3; // should be 2
    ASSERT_GE(scenario->candidate_rows.size(), 2u);
    scenario->candidate_rows[1].payload_fields = {{"ack", "Rejected"}, {"resolved_step", "3"}};

    ReplayCompare::ReplayCompareTestHarness harness;
    ReplayCompare::ReplayCompareTestHarnessOptions options{};
    options.execution_mode = ReplayCompare::ReplayCompareExecutionMode::TestValidation;
    options.feature_set = ReplayCompare::ReplayCompareFeatureSet::All();

    const auto result = harness.RunSingleScenario(*scenario, options);
    EXPECT_EQ(result.report.status, ReplayCompare::ReplayCompareStatus::Failed);
    ASSERT_TRUE(result.report.first_mismatch.has_value());
    EXPECT_EQ(result.report.first_mismatch->step_index, 1u);
    EXPECT_EQ(result.report.first_mismatch->domain, ReplayCompare::ReplayMismatchDomain::AsyncAckTimeline);
    EXPECT_EQ(result.report.first_mismatch->event_seq, 102u);

    ASSERT_TRUE(result.report.first_divergent_step.has_value());
    EXPECT_EQ(*result.report.first_divergent_step, 1u);
    ASSERT_TRUE(result.report.first_divergent_event_seq.has_value());
    EXPECT_EQ(*result.report.first_divergent_event_seq, 102u);
    ASSERT_TRUE(result.report.first_divergent_row.has_value());
    EXPECT_EQ(*result.report.first_divergent_row, 1u);
    ASSERT_TRUE(result.report.first_divergent_status.has_value());
    EXPECT_EQ(*result.report.first_divergent_status, ReplayCompare::ReplayCompareStatus::Failed);
}

TEST(V2ReplayScenarioPackTests, UnsupportedAndFallbackAreDistinguishedInFirstDivergence)
{
    auto unsupported_scenarios = ReplayCompare::V2ReplayScenarioPack::BuildCoreScenarioPack();
    auto* unsupported = FindScenario(unsupported_scenarios, "v2-vs-legacy.single-symbol");
    ASSERT_TRUE(unsupported != nullptr);
    unsupported->legacy_steps[0].state.progress.status = ReplayCompare::ReplayCompareStatus::Unsupported;
    unsupported->candidate_steps[0].state.progress.status = ReplayCompare::ReplayCompareStatus::Unsupported;

    ReplayCompare::ReplayCompareTestHarness harness;
    ReplayCompare::ReplayCompareTestHarnessOptions options{};
    options.execution_mode = ReplayCompare::ReplayCompareExecutionMode::TestValidation;
    options.feature_set = ReplayCompare::ReplayCompareTestHarness::StateOnlyFeatureSet();

    const auto unsupported_result = harness.RunSingleScenario(*unsupported, options);
    EXPECT_EQ(unsupported_result.report.status, ReplayCompare::ReplayCompareStatus::Unsupported);
    ASSERT_TRUE(unsupported_result.report.first_divergent_status.has_value());
    EXPECT_EQ(*unsupported_result.report.first_divergent_status, ReplayCompare::ReplayCompareStatus::Unsupported);
    ASSERT_TRUE(unsupported_result.report.first_divergent_step.has_value());
    EXPECT_EQ(*unsupported_result.report.first_divergent_step, 0u);

    auto fallback_scenarios = ReplayCompare::V2ReplayScenarioPack::BuildCoreScenarioPack();
    auto* fallback = FindScenario(fallback_scenarios, "v2-vs-legacy.funding-reference-edge");
    ASSERT_TRUE(fallback != nullptr);
    fallback->legacy_steps[0].state.progress.status = ReplayCompare::ReplayCompareStatus::Fallback;
    fallback->legacy_steps[0].state.progress.fallback_to_legacy = true;
    fallback->candidate_steps[0].state.progress.status = ReplayCompare::ReplayCompareStatus::Fallback;
    fallback->candidate_steps[0].state.progress.fallback_to_legacy = true;

    const auto fallback_result = harness.RunSingleScenario(*fallback, options);
    EXPECT_EQ(fallback_result.report.status, ReplayCompare::ReplayCompareStatus::Fallback);
    ASSERT_TRUE(fallback_result.report.first_divergent_status.has_value());
    EXPECT_EQ(*fallback_result.report.first_divergent_status, ReplayCompare::ReplayCompareStatus::Fallback);
    ASSERT_TRUE(fallback_result.report.first_divergent_step.has_value());
    EXPECT_EQ(*fallback_result.report.first_divergent_step, 0u);
}

TEST(V2ReplayScenarioPackTests, AlignStepBoundaryByCommonPayloadReducesMissingPayloadNoise)
{
    auto scenarios = ReplayCompare::V2ReplayScenarioPack::BuildCoreScenarioPack();
    auto* scenario = FindScenario(scenarios, "v2-vs-legacy.single-symbol");
    ASSERT_TRUE(scenario != nullptr);

    scenario->scenario.expected_steps = 0;
    auto extra = scenario->candidate_steps.back();
    extra.state.step_seq += 1;
    extra.state.ts_exchange += 60'000;
    extra.state.account.total_cash_balance += 100.0;
    scenario->candidate_steps.push_back(extra);

    ReplayCompare::ReplayCompareTestHarness harness;
    ReplayCompare::ReplayCompareTestHarnessOptions options{};
    options.execution_mode = ReplayCompare::ReplayCompareExecutionMode::TestValidation;
    options.feature_set = ReplayCompare::ReplayCompareTestHarness::StateOnlyFeatureSet();
    options.align_step_boundary_by_common_payload = true;

    const auto aligned = harness.RunSingleScenario(*scenario, options);
    EXPECT_EQ(aligned.report.status, ReplayCompare::ReplayCompareStatus::Success);

    options.align_step_boundary_by_common_payload = false;
    const auto unaligned = harness.RunSingleScenario(*scenario, options);
    EXPECT_EQ(unaligned.report.status, ReplayCompare::ReplayCompareStatus::Failed);
    ASSERT_TRUE(unaligned.report.first_mismatch.has_value());
    EXPECT_EQ(unaligned.report.first_mismatch->field, "step.payload_presence");
}

TEST(V2ReplayScenarioPackTests, TradingSemanticProtectionPackKeepsFillFundingRejectionLiquidationAndMixedFlowsStable)
{
    const auto scenarios = ReplayCompare::V2ReplayScenarioPack::BuildCoreScenarioPack();
    ReplayCompare::ReplayCompareTestHarness harness;

    ReplayCompare::ReplayCompareTestHarnessOptions options{};
    options.execution_mode = ReplayCompare::ReplayCompareExecutionMode::TestValidation;
    options.feature_set = ReplayCompare::ReplayCompareFeatureSet::All();
    options.scenario_name_filters = {
        "v2-vs-legacy.single-symbol",
        "v2-vs-legacy.basis-stress",
        "v2-vs-legacy.mixed-spot-perp",
        "v2-vs-legacy.funding-reference-edge",
        "v2-vs-legacy.rejection-liquidation",
        "v2-vs-legacy.async-ack-latency",
    };

    const auto pack_result = harness.RunScenarioPack(scenarios, options);
    ASSERT_EQ(pack_result.scenario_results.size(), 6u);
    EXPECT_TRUE(pack_result.all_passed);
    EXPECT_EQ(pack_result.failed_scenarios, 0u);
    EXPECT_EQ(pack_result.unsupported_scenarios, 0u);
    EXPECT_EQ(pack_result.fallback_scenarios, 0u);
    EXPECT_EQ(pack_result.total_mismatch_count, 0u);

    const auto* rejection_liquidation = FindScenario(scenarios, "v2-vs-legacy.rejection-liquidation");
    ASSERT_TRUE(rejection_liquidation != nullptr);
    bool has_rejection = false;
    bool has_liquidation = false;
    for (const auto& step : rejection_liquidation->legacy_steps) {
        for (const auto& event : step.event.events) {
            has_rejection = has_rejection || event.type == ReplayCompare::ReplayEventType::Rejection;
            has_liquidation = has_liquidation || event.type == ReplayCompare::ReplayEventType::Liquidation;
        }
    }
    EXPECT_TRUE(has_rejection);
    EXPECT_TRUE(has_liquidation);
}

TEST(V2ReplayScenarioPackTests, CompareDimensionsDetectStepFundingAckAndLogTimestampMismatches)
{
    ReplayCompare::ReplayCompareTestHarness harness;
    ReplayCompare::ReplayCompareTestHarnessOptions options{};
    options.execution_mode = ReplayCompare::ReplayCompareExecutionMode::TestValidation;

    auto step_scenarios = ReplayCompare::V2ReplayScenarioPack::BuildCoreScenarioPack();
    auto* mixed = FindScenario(step_scenarios, "v2-vs-legacy.mixed-spot-perp");
    ASSERT_TRUE(mixed != nullptr);
    mixed->candidate_steps[1].state.step_seq += 10;
    options.feature_set = ReplayCompare::ReplayCompareTestHarness::StateOnlyFeatureSet();
    const auto step_result = harness.RunSingleScenario(*mixed, options);
    ASSERT_TRUE(step_result.report.first_mismatch.has_value());
    EXPECT_EQ(step_result.report.first_mismatch->field, "state.step_seq");

    auto funding_scenarios = ReplayCompare::V2ReplayScenarioPack::BuildCoreScenarioPack();
    auto* funding = FindScenario(funding_scenarios, "v2-vs-legacy.funding-reference-edge");
    ASSERT_TRUE(funding != nullptr);
    funding->candidate_steps[2].event.events[0].price += 1.0;
    options.feature_set = ReplayCompare::ReplayCompareTestHarness::EventOnlyFeatureSet();
    const auto funding_result = harness.RunSingleScenario(*funding, options);
    ASSERT_TRUE(funding_result.report.first_mismatch.has_value());
    EXPECT_EQ(funding_result.report.first_mismatch->domain, ReplayCompare::ReplayMismatchDomain::Event);
    EXPECT_NE(funding_result.report.first_mismatch->field.find(".price"), std::string::npos);

    auto ack_scenarios = ReplayCompare::V2ReplayScenarioPack::BuildCoreScenarioPack();
    auto* ack = FindScenario(ack_scenarios, "v2-vs-legacy.async-ack-latency");
    ASSERT_TRUE(ack != nullptr);
    ack->candidate_steps[1].event.events[0].due_step = 3;
    options.feature_set = ReplayCompare::ReplayCompareTestHarness::AckOnlyFeatureSet();
    const auto ack_result = harness.RunSingleScenario(*ack, options);
    ASSERT_TRUE(ack_result.report.first_mismatch.has_value());
    EXPECT_EQ(ack_result.report.first_mismatch->domain, ReplayCompare::ReplayMismatchDomain::AsyncAckTimeline);

    auto row_scenarios = ReplayCompare::V2ReplayScenarioPack::BuildCoreScenarioPack();
    auto* row = FindScenario(row_scenarios, "v2-vs-legacy.funding-reference-edge");
    ASSERT_TRUE(row != nullptr);
    row->candidate_rows[0].ts_exchange += 1;
    options.feature_set = ReplayCompare::ReplayCompareTestHarness::RowOnlyFeatureSet();
    const auto row_result = harness.RunSingleScenario(*row, options);
    ASSERT_TRUE(row_result.report.first_mismatch.has_value());
    EXPECT_EQ(row_result.report.first_mismatch->domain, ReplayCompare::ReplayMismatchDomain::LogRow);
    EXPECT_EQ(row_result.report.first_mismatch->field, "row.ts_exchange");
}

TEST(V2ReplayScenarioPackTests, NumericToleranceControlsNoiseWithoutMaskingSemanticDrift)
{
    ReplayCompare::ReplayCompareTestHarness harness;
    ReplayCompare::ReplayCompareTestHarnessOptions options{};
    options.execution_mode = ReplayCompare::ReplayCompareExecutionMode::TestValidation;
    options.feature_set = ReplayCompare::ReplayCompareTestHarness::StateOnlyFeatureSet();

    auto tolerant_scenarios = ReplayCompare::V2ReplayScenarioPack::BuildCoreScenarioPack();
    auto* tolerant = FindScenario(tolerant_scenarios, "v2-vs-legacy.single-symbol");
    ASSERT_TRUE(tolerant != nullptr);
    tolerant->candidate_steps[1].state.account.total_cash_balance += 5e-10;
    const auto tolerant_result = harness.RunSingleScenario(*tolerant, options);
    EXPECT_EQ(tolerant_result.report.status, ReplayCompare::ReplayCompareStatus::Success);

    auto strict_scenarios = ReplayCompare::V2ReplayScenarioPack::BuildCoreScenarioPack();
    auto* strict = FindScenario(strict_scenarios, "v2-vs-legacy.single-symbol");
    ASSERT_TRUE(strict != nullptr);
    strict->candidate_steps[1].state.account.total_cash_balance += 1e-3;
    const auto strict_result = harness.RunSingleScenario(*strict, options);
    EXPECT_EQ(strict_result.report.status, ReplayCompare::ReplayCompareStatus::Failed);
    ASSERT_TRUE(strict_result.report.first_mismatch.has_value());
    EXPECT_EQ(strict_result.report.first_mismatch->field, "account.total_cash_balance");
}
