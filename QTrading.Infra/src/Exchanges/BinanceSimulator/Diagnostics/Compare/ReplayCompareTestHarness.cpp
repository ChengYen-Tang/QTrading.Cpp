#include "Exchanges/BinanceSimulator/Diagnostics/Compare/ReplayCompareTestHarness.hpp"

#include <algorithm>
#include <memory>
#include <string_view>

namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare {

namespace {

int StatusRank(ReplayCompareStatus status)
{
    switch (status) {
    case ReplayCompareStatus::Success:
        return 0;
    case ReplayCompareStatus::Unsupported:
        return 1;
    case ReplayCompareStatus::Fallback:
        return 2;
    case ReplayCompareStatus::Failed:
        return 3;
    }
    return 3;
}

ReplayCompareStatus PromoteStatus(ReplayCompareStatus lhs, ReplayCompareStatus rhs)
{
    return (StatusRank(rhs) > StatusRank(lhs)) ? rhs : lhs;
}

} // namespace

ReplayCompareHarnessResult ReplayCompareTestHarness::RunSingleScenario(
    const ReplayCompareScenarioData& scenario_data,
    ReplayCompareTestHarnessOptions options) const
{
    options = NormalizeOptions(std::move(options));

    ReplayScenario scenario = scenario_data.scenario;
    if (scenario.name.empty()) {
        scenario.name = "unnamed_scenario";
    }
    if (scenario.dataset_id.empty()) {
        scenario.dataset_id = "compare-test-dataset";
    }
    const uint64_t inferred_steps = static_cast<uint64_t>(
        std::max(scenario_data.legacy_steps.size(), scenario_data.candidate_steps.size()));
    if (scenario.expected_steps == 0) {
        scenario.expected_steps = inferred_steps;
    }
    if (scenario.end_ts_exchange < scenario.start_ts_exchange) {
        scenario.end_ts_exchange = scenario.start_ts_exchange;
    }

    const bool dual_run_enabled = options.execution_mode == ReplayCompareExecutionMode::TestValidation;
    ReplayCoreInputBundle input{};
    input.scenario = scenario;
    input.config.feature_set = options.feature_set;
    input.config.stop_on_first_mismatch = false;
    input.config.allow_fallback_to_legacy = true;
    input.config.max_mismatches = 0;
    input.run_id = options.run_id_override != 0 ? options.run_id_override : scenario_data.scenario.expected_steps + 1;
    input.random_seed = options.random_seed;
    input.legacy_core_state = std::make_shared<int>(1);
    if (dual_run_enabled) {
        input.candidate_core_state = std::make_shared<int>(2);
    } else {
        input.candidate_core_state.reset();
    }

    const StepCompareRules step_rules = BuildEffectiveStepRules(options.step_rules, options.feature_set);
    StepCompareModel step_model(step_rules);

    DifferentialReplayRunner::Callbacks callbacks{};
    callbacks.run_legacy_step = [](const ReplayCoreInputBundle&, uint64_t) {};
    if (dual_run_enabled) {
        callbacks.run_candidate_step = [](const ReplayCoreInputBundle&, uint64_t) {};
        callbacks.compare_step = [&](const ReplayCoreInputBundle&, uint64_t step_index) {
            ReplayStepCompareResult result{};
            result.step_index = step_index;
            result.compared = true;
            result.status = ReplayCompareStatus::Success;

            const bool has_legacy = step_index < scenario_data.legacy_steps.size();
            const bool has_candidate = step_index < scenario_data.candidate_steps.size();
            if (!has_legacy || !has_candidate) {
                result.status = ReplayCompareStatus::Failed;
                result.matched = false;
                result.note = "step payload missing";
                ReplayMismatch mismatch{};
                mismatch.domain = ReplayMismatchDomain::Orchestration;
                mismatch.field = "step.payload_presence";
                mismatch.reason = "payload presence mismatch";
                mismatch.legacy_value = has_legacy ? "present" : "absent";
                mismatch.candidate_value = has_candidate ? "present" : "absent";
                mismatch.step_index = step_index;
                result.mismatches.push_back(std::move(mismatch));
                return result;
            }

            return step_model.CompareStep(
                step_index,
                scenario_data.legacy_steps[step_index],
                scenario_data.candidate_steps[step_index]);
        };
    }

    DifferentialReplayRunner runner;
    ReplayCompareReport report = runner.Run(input, callbacks);

    std::optional<LegacyLogRowCompareReport> row_report;
    if (dual_run_enabled && options.feature_set.Has(ReplayCompareFeature::LogRow)) {
        LegacyLogRowComparer row_comparer;
        row_report = row_comparer.Compare(
            scenario_data.legacy_rows,
            scenario_data.candidate_rows,
            options.row_rules);
        MergeRowCompareIntoReport(report, *row_report);
    }

    if (options.execution_mode == ReplayCompareExecutionMode::TestValidation &&
        options.include_legacy_row_snapshot) {
        report.legacy_row_snapshot_lines = scenario_data.legacy_row_snapshot_lines;
    } else {
        report.legacy_row_snapshot_lines.clear();
    }

    DiagnosticFormatOptions format_options{};
    format_options.mode = options.human_readable_mode;
    format_options.summary_mismatch_limit = 3;
    format_options.include_legacy_row_snapshot =
        options.include_legacy_row_snapshot &&
        options.execution_mode == ReplayCompareExecutionMode::TestValidation;

    ReplayCompareHarnessResult result{};
    result.report = report;
    result.row_report = row_report;
    result.dual_run_enabled = dual_run_enabled;
    result.human_readable_report = DiagnosticReportFormatter::FormatHumanReadable(
        result.report,
        row_report.has_value() ? &*row_report : nullptr,
        format_options);

    if (options.generate_artifact) {
        DiagnosticReportMode artifact_mode = options.artifact_mode;
        if (options.execution_mode != ReplayCompareExecutionMode::TestValidation) {
            artifact_mode = DiagnosticReportMode::Summary;
        }
        result.artifact_json = DiagnosticReportFormatter::SerializeArtifactJson(
            result.report,
            row_report.has_value() ? &*row_report : nullptr,
            artifact_mode);
        result.heavy_artifact_generated =
            options.execution_mode == ReplayCompareExecutionMode::TestValidation &&
            artifact_mode == DiagnosticReportMode::Detailed;
    }

    return result;
}

ReplayCompareHarnessResult ReplayCompareTestHarness::RunSingleScenarioByFeatureFilter(
    const ReplayCompareScenarioData& scenario_data,
    ReplayCompareFeatureSet feature_filter,
    ReplayCompareTestHarnessOptions options) const
{
    options.feature_set = feature_filter;
    return RunSingleScenario(scenario_data, std::move(options));
}

ReplayCompareScenarioPackResult ReplayCompareTestHarness::RunScenarioPack(
    const std::vector<ReplayCompareScenarioData>& scenarios,
    ReplayCompareTestHarnessOptions options) const
{
    options = NormalizeOptions(std::move(options));

    ReplayCompareScenarioPackResult out{};
    out.scenario_results.reserve(scenarios.size());
    for (const auto& scenario_data : scenarios) {
        if (!ShouldIncludeScenario(scenario_data, options)) {
            continue;
        }
        auto result = RunSingleScenario(scenario_data, options);
        out.total_mismatch_count += result.report.mismatch_count;
        if (result.report.status == ReplayCompareStatus::Failed) {
            ++out.failed_scenarios;
        } else if (result.report.status == ReplayCompareStatus::Unsupported) {
            ++out.unsupported_scenarios;
        } else if (result.report.status == ReplayCompareStatus::Fallback) {
            ++out.fallback_scenarios;
        }
        out.scenario_results.push_back(std::move(result));
    }

    out.all_passed =
        !out.scenario_results.empty() &&
        out.failed_scenarios == 0 &&
        out.unsupported_scenarios == 0 &&
        out.fallback_scenarios == 0;
    return out;
}

ReplayCompareScenarioPackResult ReplayCompareTestHarness::RunScenarioPackByFeatureFilter(
    const std::vector<ReplayCompareScenarioData>& scenarios,
    ReplayCompareFeatureSet feature_filter,
    ReplayCompareTestHarnessOptions options) const
{
    options.feature_set = feature_filter;
    return RunScenarioPack(scenarios, std::move(options));
}

ReplayCompareFeatureSet ReplayCompareTestHarness::BuildFeatureSet(
    std::initializer_list<ReplayCompareFeature> features)
{
    ReplayCompareFeatureSet out{};
    out.bits = static_cast<uint32_t>(ReplayCompareFeature::None);
    for (const auto feature : features) {
        out.Enable(feature);
    }
    return out;
}

ReplayCompareFeatureSet ReplayCompareTestHarness::StateOnlyFeatureSet()
{
    return BuildFeatureSet({ ReplayCompareFeature::State });
}

ReplayCompareFeatureSet ReplayCompareTestHarness::EventOnlyFeatureSet()
{
    return BuildFeatureSet({ ReplayCompareFeature::Event });
}

ReplayCompareFeatureSet ReplayCompareTestHarness::RowOnlyFeatureSet()
{
    return BuildFeatureSet({ ReplayCompareFeature::LogRow });
}

ReplayCompareFeatureSet ReplayCompareTestHarness::AckOnlyFeatureSet()
{
    return BuildFeatureSet({ ReplayCompareFeature::AsyncAckTimeline });
}

ReplayCompareTestHarnessOptions ReplayCompareTestHarness::NormalizeOptions(
    ReplayCompareTestHarnessOptions options)
{
    if (options.execution_mode != ReplayCompareExecutionMode::TestValidation) {
        options.human_readable_mode = DiagnosticReportMode::Summary;
        options.artifact_mode = DiagnosticReportMode::Summary;
        options.include_legacy_row_snapshot = false;
    }
    return options;
}

StepCompareRules ReplayCompareTestHarness::BuildEffectiveStepRules(
    const StepCompareRules& base_rules,
    ReplayCompareFeatureSet feature_set)
{
    StepCompareRules rules = base_rules;
    const bool state_enabled = feature_set.Has(ReplayCompareFeature::State);
    const bool event_enabled = feature_set.Has(ReplayCompareFeature::Event) ||
        feature_set.Has(ReplayCompareFeature::AsyncAckTimeline);
    const bool ack_enabled = feature_set.Has(ReplayCompareFeature::AsyncAckTimeline);
    const bool non_ack_event_enabled = feature_set.Has(ReplayCompareFeature::Event);

    rules.state.enabled = state_enabled;
    rules.event.enabled = event_enabled;
    rules.event.compare_fill = non_ack_event_enabled;
    rules.event.compare_funding = non_ack_event_enabled;
    rules.event.compare_rejection_liquidation = non_ack_event_enabled;
    rules.event.compare_async_ack_timeline = ack_enabled;
    rules.event.strict_async_ack_timeline = ack_enabled && rules.event.strict_async_ack_timeline;
    return rules;
}

bool ReplayCompareTestHarness::ShouldIncludeScenario(
    const ReplayCompareScenarioData& scenario_data,
    const ReplayCompareTestHarnessOptions& options)
{
    if (options.scenario_name_filters.empty()) {
        return true;
    }

    return std::find(
        options.scenario_name_filters.begin(),
        options.scenario_name_filters.end(),
        scenario_data.scenario.name) != options.scenario_name_filters.end();
}

void ReplayCompareTestHarness::MergeRowCompareIntoReport(
    ReplayCompareReport& report,
    const LegacyLogRowCompareReport& row_report)
{
    if (row_report.matched) {
        return;
    }

    report.status = PromoteStatus(report.status, ReplayCompareStatus::Failed);
    for (const auto& mismatch : row_report.mismatches) {
        if (!report.first_mismatch.has_value()) {
            report.first_mismatch = mismatch;
        }
        report.mismatches.push_back(mismatch);
        ++report.mismatch_count;
    }

    if (report.summary.empty() || report.summary == "runner completed") {
        report.summary = "row compare mismatch";
    } else {
        report.summary += " + row compare mismatch";
    }
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare
