#include "Exchanges/BinanceSimulator/Diagnostics/Compare/DifferentialReplayRunner.hpp"

#include <exception>

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

ReplayMismatch BuildOrchestrationMismatch(
    uint64_t step_index,
    const std::string& reason)
{
    ReplayMismatch mismatch{};
    mismatch.domain = ReplayMismatchDomain::Orchestration;
    mismatch.step_index = step_index;
    mismatch.field = "runner.orchestration";
    mismatch.reason = reason;
    return mismatch;
}

void MergeStepIntoReport(
    const ReplayRunnerConfig& config,
    ReplayStepCompareResult step_result,
    ReplayCompareReport& report)
{
    report.status = (StatusRank(step_result.status) > StatusRank(report.status))
        ? step_result.status
        : report.status;

    if (step_result.compared) {
        ++report.compared_steps;
    }

    for (auto& mismatch : step_result.mismatches) {
        if (mismatch.step_index == ReplayMismatch::kUnspecifiedIndex) {
            mismatch.step_index = step_result.step_index;
        }
        if (!report.first_mismatch.has_value()) {
            report.first_mismatch = mismatch;
        }
        report.mismatches.push_back(mismatch);
        ++report.mismatch_count;
        if (config.max_mismatches > 0 && report.mismatch_count >= config.max_mismatches) {
            break;
        }
    }

    report.steps.push_back(std::move(step_result));
}

} // namespace

bool ReplayScenario::IsDeterministic() const
{
    if (name.empty() || dataset_id.empty()) {
        return false;
    }
    if (expected_steps == 0) {
        return false;
    }
    return start_ts_exchange <= end_ts_exchange;
}

bool ReplayCoreInputBundle::HasIndependentCoreStates() const
{
    if (!legacy_core_state || !candidate_core_state) {
        return true;
    }
    return legacy_core_state.get() != candidate_core_state.get();
}

ReplayCompareReport DifferentialReplayRunner::Run(
    const ReplayCoreInputBundle& input,
    const Callbacks& callbacks) const
{
    ReplayCompareReport report{};
    report.scenario_name = input.scenario.name;
    report.run_id = input.run_id;
    report.feature_set = input.config.feature_set;
    report.status = ReplayCompareStatus::Success;

    if (!input.scenario.IsDeterministic()) {
        report.status = ReplayCompareStatus::Unsupported;
        report.summary = "scenario is not deterministic";
        return report;
    }

    if (!callbacks.run_legacy_step) {
        report.status = ReplayCompareStatus::Unsupported;
        report.summary = "legacy core callback is required";
        return report;
    }

    if (callbacks.run_candidate_step && !input.HasIndependentCoreStates()) {
        report.status = ReplayCompareStatus::Unsupported;
        report.summary = "legacy and candidate core states must be independent";
        return report;
    }

    const bool has_candidate = static_cast<bool>(callbacks.run_candidate_step);
    const bool can_compare = has_candidate &&
        static_cast<bool>(callbacks.compare_step) &&
        !input.config.feature_set.Empty();

    for (uint64_t step_index = 0; step_index < input.scenario.expected_steps; ++step_index) {
        ReplayStepCompareResult step_result{};
        step_result.step_index = step_index;

        try {
            callbacks.run_legacy_step(input, step_index);
            ++report.legacy_steps_executed;
            report.legacy_baseline_preserved = true;
        } catch (const std::exception& ex) {
            step_result.status = ReplayCompareStatus::Failed;
            step_result.matched = false;
            step_result.note = ex.what();
            step_result.mismatches.push_back(
                BuildOrchestrationMismatch(step_index, "legacy core step threw exception"));
            MergeStepIntoReport(input.config, std::move(step_result), report);
            report.legacy_baseline_preserved = false;
            report.summary = "legacy baseline failed";
            break;
        } catch (...) {
            step_result.status = ReplayCompareStatus::Failed;
            step_result.matched = false;
            step_result.note = "unknown legacy exception";
            step_result.mismatches.push_back(
                BuildOrchestrationMismatch(step_index, "legacy core step threw unknown exception"));
            MergeStepIntoReport(input.config, std::move(step_result), report);
            report.legacy_baseline_preserved = false;
            report.summary = "legacy baseline failed";
            break;
        }

        if (!has_candidate) {
            step_result.status = ReplayCompareStatus::Fallback;
            step_result.compared = false;
            step_result.matched = true;
            step_result.fallback_to_legacy = true;
            step_result.note = "candidate core not configured";
            MergeStepIntoReport(input.config, std::move(step_result), report);
            continue;
        }

        bool candidate_step_ok = true;
        try {
            callbacks.run_candidate_step(input, step_index);
            ++report.candidate_steps_executed;
        } catch (const std::exception& ex) {
            candidate_step_ok = false;
            step_result.note = ex.what();
        } catch (...) {
            candidate_step_ok = false;
            step_result.note = "unknown candidate exception";
        }

        if (!candidate_step_ok) {
            if (input.config.allow_fallback_to_legacy) {
                step_result.status = ReplayCompareStatus::Fallback;
                step_result.compared = false;
                step_result.matched = true;
                step_result.fallback_to_legacy = true;
                if (step_result.note.empty()) {
                    step_result.note = "candidate core failure";
                }
            } else {
                step_result.status = ReplayCompareStatus::Failed;
                step_result.compared = false;
                step_result.matched = false;
                step_result.fallback_to_legacy = false;
                step_result.mismatches.push_back(
                    BuildOrchestrationMismatch(step_index, "candidate core step failed"));
            }
            MergeStepIntoReport(input.config, std::move(step_result), report);
            if (input.config.stop_on_first_mismatch &&
                report.status == ReplayCompareStatus::Failed) {
                report.summary = "stopped after candidate step failure";
                break;
            }
            continue;
        }

        if (!can_compare) {
            step_result.status = ReplayCompareStatus::Unsupported;
            step_result.compared = false;
            step_result.matched = true;
            step_result.note = "compare callback or feature set not configured";
            MergeStepIntoReport(input.config, std::move(step_result), report);
            continue;
        }

        step_result = callbacks.compare_step(input, step_index);
        NormalizeStepResult(step_result, step_index);
        MergeStepIntoReport(input.config, std::move(step_result), report);

        if (input.config.stop_on_first_mismatch &&
            report.first_mismatch.has_value()) {
            report.summary = "stopped at first mismatch";
            break;
        }

        if (input.config.max_mismatches > 0 && report.mismatch_count >= input.config.max_mismatches) {
            report.summary = "reached mismatch cap";
            break;
        }
    }

    if (report.steps.empty() && report.status == ReplayCompareStatus::Success) {
        report.status = ReplayCompareStatus::Unsupported;
        report.summary = "no replay steps executed";
    } else if (report.summary.empty()) {
        report.summary = "runner completed";
    }

    return report;
}

ReplayCompareStatus DifferentialReplayRunner::PromoteStatus(
    ReplayCompareStatus aggregate,
    ReplayCompareStatus step)
{
    return (StatusRank(step) > StatusRank(aggregate)) ? step : aggregate;
}

void DifferentialReplayRunner::NormalizeStepResult(
    ReplayStepCompareResult& step_result,
    uint64_t step_index)
{
    step_result.step_index = step_index;

    if (!step_result.mismatches.empty()) {
        step_result.compared = true;
        step_result.matched = false;
        if (step_result.status == ReplayCompareStatus::Success) {
            step_result.status = ReplayCompareStatus::Failed;
        }
    }

    if (step_result.status == ReplayCompareStatus::Failed) {
        step_result.matched = false;
        step_result.fallback_to_legacy = false;
    } else if (step_result.status == ReplayCompareStatus::Fallback) {
        step_result.compared = false;
        step_result.matched = true;
        step_result.fallback_to_legacy = true;
    } else if (step_result.status == ReplayCompareStatus::Unsupported) {
        step_result.compared = false;
        step_result.fallback_to_legacy = false;
    } else if (step_result.status == ReplayCompareStatus::Success) {
        if (!step_result.compared) {
            step_result.compared = true;
        }
        step_result.matched = true;
        step_result.fallback_to_legacy = false;
    }
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare
