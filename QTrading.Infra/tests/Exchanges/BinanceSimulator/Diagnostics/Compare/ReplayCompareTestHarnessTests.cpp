#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Exchanges/BinanceSimulator/Diagnostics/Compare/ReplayCompareTestHarness.hpp"

namespace ReplayCompare = QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare;

namespace {

ReplayCompare::StepComparePayload BuildStep(
    uint64_t step_seq,
    uint64_t ts_exchange,
    double cash,
    uint64_t order_count)
{
    ReplayCompare::StepComparePayload step{};
    step.state.step_seq = step_seq;
    step.state.ts_exchange = ts_exchange;
    step.state.progress.progressed = true;
    step.state.progress.status = ReplayCompare::ReplayCompareStatus::Success;
    step.state.account.perp_wallet_balance = cash;
    step.state.account.spot_wallet_balance = 0.0;
    step.state.account.total_cash_balance = cash;
    step.state.account.total_ledger_value = cash;
    step.state.account.total_ledger_value_base = cash;
    step.state.account.total_ledger_value_conservative = cash;
    step.state.account.total_ledger_value_optimistic = cash;
    step.state.order.open_order_count = order_count;
    step.state.position.position_count = 1;
    step.state.position.gross_position_notional = 100.0;
    step.state.position.net_position_notional = 10.0;
    return step;
}

ReplayCompare::ReplayEventSummary BuildEvent(
    ReplayCompare::ReplayEventType type,
    uint64_t event_seq,
    double amount = 0.0)
{
    ReplayCompare::ReplayEventSummary event{};
    event.type = type;
    event.event_seq = event_seq;
    event.ts_exchange = 1000;
    event.ts_local = 1001;
    event.symbol = "BTCUSDT";
    event.event_id = "evt-" + std::to_string(event_seq);
    event.quantity = 1.0;
    event.price = 100.0;
    event.amount = amount;
    event.status = "Accepted";
    event.request_id = event_seq;
    event.submitted_step = 1;
    event.due_step = 2;
    event.resolved_step = 2;
    return event;
}

ReplayCompare::LegacyLogCompareRow BuildRow(
    uint64_t arrival_index,
    int32_t module_id,
    std::string module_name,
    uint64_t step_seq,
    uint64_t event_seq,
    std::string payload_value = "v")
{
    ReplayCompare::LegacyLogCompareRow row{};
    row.arrival_index = arrival_index;
    row.batch_boundary = 0;
    row.module_id = module_id;
    row.module_name = std::move(module_name);
    row.ts_exchange = 1000;
    row.ts_local = 1001;
    row.run_id = 1;
    row.step_seq = step_seq;
    row.event_seq = event_seq;
    row.symbol = "BTCUSDT";
    row.row_kind = ReplayCompare::LegacyLogRowKind::Event;
    row.payload_fields = { {"k", std::move(payload_value)} };
    return row;
}

ReplayCompare::ReplayCompareScenarioData BuildScenarioData(const std::string& name)
{
    ReplayCompare::ReplayCompareScenarioData scenario{};
    scenario.scenario.name = name;
    scenario.scenario.dataset_id = "dataset";
    scenario.scenario.start_ts_exchange = 1000;
    scenario.scenario.end_ts_exchange = 2000;
    scenario.scenario.expected_steps = 1;
    scenario.legacy_steps = { BuildStep(1, 1000, 1000.0, 1) };
    scenario.candidate_steps = scenario.legacy_steps;
    scenario.legacy_steps[0].event.events = {
        BuildEvent(ReplayCompare::ReplayEventType::Fill, 1, 0.0),
        BuildEvent(ReplayCompare::ReplayEventType::Funding, 2, -0.12),
        BuildEvent(ReplayCompare::ReplayEventType::AsyncAck, 3, 0.0),
    };
    scenario.candidate_steps[0].event.events = scenario.legacy_steps[0].event.events;
    scenario.legacy_rows = { BuildRow(0, 10, "AccountEvent", 1, 1, "ok") };
    scenario.candidate_rows = scenario.legacy_rows;
    scenario.legacy_row_snapshot_lines = { "legacy row snapshot" };
    return scenario;
}

} // namespace

TEST(ReplayCompareTestHarnessTests, SingleScenarioSmokeCompareHasDirectRunnerApi)
{
    ReplayCompare::ReplayCompareTestHarness harness;
    auto scenario = BuildScenarioData("single-smoke");

    ReplayCompare::ReplayCompareTestHarnessOptions options{};
    options.execution_mode = ReplayCompare::ReplayCompareExecutionMode::TestValidation;
    options.feature_set = ReplayCompare::ReplayCompareFeatureSet::Core();
    options.human_readable_mode = ReplayCompare::DiagnosticReportMode::Detailed;

    const auto result = harness.RunSingleScenario(scenario, options);
    EXPECT_TRUE(result.dual_run_enabled);
    EXPECT_EQ(result.report.status, ReplayCompare::ReplayCompareStatus::Success);
    EXPECT_EQ(result.report.mismatch_count, 0u);
    EXPECT_NE(result.human_readable_report.find("scenario=single-smoke"), std::string::npos);
}

TEST(ReplayCompareTestHarnessTests, ScenarioPackEntryRunsMultipleScenarios)
{
    ReplayCompare::ReplayCompareTestHarness harness;
    auto scenario_a = BuildScenarioData("pack-a");
    auto scenario_b = BuildScenarioData("pack-b");
    scenario_b.candidate_steps[0].state.account.total_cash_balance = 990.0;

    ReplayCompare::ReplayCompareTestHarnessOptions options{};
    options.execution_mode = ReplayCompare::ReplayCompareExecutionMode::TestValidation;
    options.feature_set = ReplayCompare::ReplayCompareTestHarness::StateOnlyFeatureSet();

    const auto pack = harness.RunScenarioPack({scenario_a, scenario_b}, options);
    ASSERT_EQ(pack.scenario_results.size(), 2u);
    EXPECT_EQ(pack.failed_scenarios, 1u);
    EXPECT_EQ(pack.unsupported_scenarios, 0u);
    EXPECT_EQ(pack.fallback_scenarios, 0u);
    EXPECT_FALSE(pack.all_passed);
}

TEST(ReplayCompareTestHarnessTests, DualSymbolHolesReplayCompareDetectsMissingEvent)
{
    ReplayCompare::ReplayCompareTestHarness harness;
    auto scenario = BuildScenarioData("dual-symbol-holes");

    auto btc_fill = BuildEvent(ReplayCompare::ReplayEventType::Fill, 1, 0.0);
    auto eth_fill = BuildEvent(ReplayCompare::ReplayEventType::Fill, 2, 0.0);
    eth_fill.symbol = "ETHUSDT";
    eth_fill.event_id = "evt-eth-2";
    scenario.legacy_steps[0].event.events = { btc_fill, eth_fill };
    scenario.candidate_steps[0].event.events = { btc_fill }; // hole on ETH

    ReplayCompare::ReplayCompareTestHarnessOptions options{};
    options.execution_mode = ReplayCompare::ReplayCompareExecutionMode::TestValidation;

    const auto result = harness.RunSingleScenarioByFeatureFilter(
        scenario,
        ReplayCompare::ReplayCompareTestHarness::EventOnlyFeatureSet(),
        options);
    EXPECT_EQ(result.report.status, ReplayCompare::ReplayCompareStatus::Failed);
    ASSERT_TRUE(result.report.first_mismatch.has_value());
    EXPECT_EQ(result.report.first_mismatch->domain, ReplayCompare::ReplayMismatchDomain::Event);
    EXPECT_EQ(result.report.first_mismatch->field, "event.count");
}

TEST(ReplayCompareTestHarnessTests, FundingReplayCompareDetectsFundingAmountMismatch)
{
    ReplayCompare::ReplayCompareTestHarness harness;
    auto scenario = BuildScenarioData("funding-replay");

    auto funding_event = BuildEvent(ReplayCompare::ReplayEventType::Funding, 7, -0.10);
    scenario.legacy_steps[0].event.events = { funding_event };
    scenario.candidate_steps[0].event.events = { funding_event };
    scenario.candidate_steps[0].event.events[0].amount = -0.15;

    ReplayCompare::ReplayCompareTestHarnessOptions options{};
    options.execution_mode = ReplayCompare::ReplayCompareExecutionMode::TestValidation;

    const auto result = harness.RunSingleScenarioByFeatureFilter(
        scenario,
        ReplayCompare::ReplayCompareTestHarness::EventOnlyFeatureSet(),
        options);
    EXPECT_EQ(result.report.status, ReplayCompare::ReplayCompareStatus::Failed);
    ASSERT_TRUE(result.report.first_mismatch.has_value());
    EXPECT_EQ(result.report.first_mismatch->domain, ReplayCompare::ReplayMismatchDomain::Event);
    EXPECT_NE(result.report.first_mismatch->field.find(".amount"), std::string::npos);
}

TEST(ReplayCompareTestHarnessTests, FeatureFilterCanRunOnlyStateEventOrRow)
{
    ReplayCompare::ReplayCompareTestHarness harness;
    auto scenario = BuildScenarioData("filter-scenario");
    scenario.candidate_steps[0].state.account.total_cash_balance = 900.0; // state mismatch
    scenario.candidate_steps[0].event.events[1].amount = -0.11; // event mismatch
    scenario.candidate_rows[0].payload_fields = { {"k", "bad"} }; // row mismatch

    ReplayCompare::ReplayCompareTestHarnessOptions options{};
    options.execution_mode = ReplayCompare::ReplayCompareExecutionMode::TestValidation;

    auto state_result = harness.RunSingleScenarioByFeatureFilter(
        scenario,
        ReplayCompare::ReplayCompareTestHarness::StateOnlyFeatureSet(),
        options);
    ASSERT_TRUE(state_result.report.first_mismatch.has_value());
    EXPECT_EQ(state_result.report.first_mismatch->domain, ReplayCompare::ReplayMismatchDomain::State);

    auto event_result = harness.RunSingleScenarioByFeatureFilter(
        scenario,
        ReplayCompare::ReplayCompareTestHarness::EventOnlyFeatureSet(),
        options);
    ASSERT_TRUE(event_result.report.first_mismatch.has_value());
    EXPECT_EQ(event_result.report.first_mismatch->domain, ReplayCompare::ReplayMismatchDomain::Event);

    auto row_result = harness.RunSingleScenarioByFeatureFilter(
        scenario,
        ReplayCompare::ReplayCompareTestHarness::RowOnlyFeatureSet(),
        options);
    ASSERT_TRUE(row_result.report.first_mismatch.has_value());
    EXPECT_EQ(row_result.report.first_mismatch->domain, ReplayCompare::ReplayMismatchDomain::LogRow);
}

TEST(ReplayCompareTestHarnessTests, FeatureFilterSupportsAckOnlyCompare)
{
    ReplayCompare::ReplayCompareTestHarness harness;
    auto scenario = BuildScenarioData("ack-only");
    scenario.candidate_steps[0].event.events[0].price = 101.0; // fill mismatch

    ReplayCompare::ReplayCompareTestHarnessOptions options{};
    options.execution_mode = ReplayCompare::ReplayCompareExecutionMode::TestValidation;

    const auto no_ack_mismatch = harness.RunSingleScenarioByFeatureFilter(
        scenario,
        ReplayCompare::ReplayCompareTestHarness::AckOnlyFeatureSet(),
        options);
    EXPECT_EQ(no_ack_mismatch.report.status, ReplayCompare::ReplayCompareStatus::Success);
    EXPECT_EQ(no_ack_mismatch.report.mismatch_count, 0u);

    scenario.candidate_steps[0].event.events[2].resolved_step = 3; // async ack mismatch
    const auto ack_mismatch = harness.RunSingleScenarioByFeatureFilter(
        scenario,
        ReplayCompare::ReplayCompareTestHarness::AckOnlyFeatureSet(),
        options);
    EXPECT_EQ(ack_mismatch.report.status, ReplayCompare::ReplayCompareStatus::Failed);
    ASSERT_TRUE(ack_mismatch.report.first_mismatch.has_value());
    EXPECT_EQ(
        ack_mismatch.report.first_mismatch->domain,
        ReplayCompare::ReplayMismatchDomain::AsyncAckTimeline);
}

TEST(ReplayCompareTestHarnessTests, ScenarioPackEntrySupportsScenarioNameFilter)
{
    ReplayCompare::ReplayCompareTestHarness harness;
    auto scenario_a = BuildScenarioData("pack-filter-a");
    auto scenario_b = BuildScenarioData("pack-filter-b");
    scenario_b.candidate_steps[0].state.account.total_cash_balance = 910.0;
    auto scenario_c = BuildScenarioData("pack-filter-c");

    ReplayCompare::ReplayCompareTestHarnessOptions options{};
    options.execution_mode = ReplayCompare::ReplayCompareExecutionMode::TestValidation;
    options.feature_set = ReplayCompare::ReplayCompareTestHarness::StateOnlyFeatureSet();
    options.scenario_name_filters = { "pack-filter-b" };

    const auto pack = harness.RunScenarioPack({scenario_a, scenario_b, scenario_c}, options);
    ASSERT_EQ(pack.scenario_results.size(), 1u);
    EXPECT_EQ(pack.scenario_results[0].report.scenario_name, "pack-filter-b");
    EXPECT_EQ(pack.failed_scenarios, 1u);
}

TEST(ReplayCompareTestHarnessTests, ProductionSafeGuardDisablesDualRunAndHeavyArtifactByDefault)
{
    ReplayCompare::ReplayCompareTestHarness harness;
    auto scenario = BuildScenarioData("production-safe");

    ReplayCompare::ReplayCompareTestHarnessOptions options{};
    options.execution_mode = ReplayCompare::ReplayCompareExecutionMode::ProductionSafe;
    options.generate_artifact = true;
    options.artifact_mode = ReplayCompare::DiagnosticReportMode::Detailed;
    options.include_legacy_row_snapshot = true;

    const auto result = harness.RunSingleScenario(scenario, options);
    EXPECT_FALSE(result.dual_run_enabled);
    EXPECT_EQ(result.report.status, ReplayCompare::ReplayCompareStatus::Fallback);
    EXPECT_FALSE(result.heavy_artifact_generated);
    ASSERT_TRUE(result.artifact_json.has_value());
    EXPECT_NE(result.artifact_json->find("\"legacy_row_snapshot_lines\":[]"), std::string::npos);
    EXPECT_TRUE(result.report.legacy_row_snapshot_lines.empty());
}

TEST(ReplayCompareTestHarnessTests, DetailedArtifactGeneratedOnlyInTestValidationMode)
{
    ReplayCompare::ReplayCompareTestHarness harness;
    auto scenario = BuildScenarioData("artifact-mode");

    ReplayCompare::ReplayCompareTestHarnessOptions test_options{};
    test_options.execution_mode = ReplayCompare::ReplayCompareExecutionMode::TestValidation;
    test_options.generate_artifact = true;
    test_options.artifact_mode = ReplayCompare::DiagnosticReportMode::Detailed;
    test_options.include_legacy_row_snapshot = true;

    const auto test_result = harness.RunSingleScenario(scenario, test_options);
    EXPECT_TRUE(test_result.heavy_artifact_generated);
    ASSERT_TRUE(test_result.artifact_json.has_value());
    EXPECT_NE(test_result.artifact_json->find("\"legacy_row_snapshot_lines\":[\"legacy row snapshot\"]"), std::string::npos);

    ReplayCompare::ReplayCompareTestHarnessOptions prod_options = test_options;
    prod_options.execution_mode = ReplayCompare::ReplayCompareExecutionMode::ProductionSafe;
    const auto prod_result = harness.RunSingleScenario(scenario, prod_options);
    EXPECT_FALSE(prod_result.heavy_artifact_generated);
    ASSERT_TRUE(prod_result.artifact_json.has_value());
    EXPECT_NE(prod_result.artifact_json->find("\"legacy_row_snapshot_lines\":[]"), std::string::npos);
}
