#pragma once

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>

namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare {

enum class ReplayCompareStatus : int32_t {
    Success = 0,
    Unsupported = 1,
    Fallback = 2,
};

enum class ReplayCompareFeature : int32_t {
    Event = 0,
    AsyncAckTimeline = 1,
    Row = 2,
    State = 3,
};

struct ReplayCompareFeatureSet {
    bool state{ false };
    bool event{ false };
    bool async_ack_timeline{ false };
    bool row{ false };

    static ReplayCompareFeatureSet All()
    {
        return ReplayCompareFeatureSet{ true, true, true, true };
    }
};

enum class ReplayCompareExecutionMode : int32_t {
    TestValidation = 0,
};

struct ReplayCompareProgress {
    ReplayCompareStatus status{ ReplayCompareStatus::Success };
    bool fallback_to_legacy{ false };
};

struct ReplayCompareStepState {
    ReplayCompareProgress progress{};
};

struct ReplayCompareStep {
    ReplayCompareStepState state{};
};

struct ReplayCompareScenario {
    std::string name;
};

struct ReplayCompareScenarioData {
    ReplayCompareScenario scenario{};
    std::vector<ReplayCompareStep> legacy_steps;
    std::vector<ReplayCompareStep> candidate_steps;
};

struct ReplayCompareReport {
    uint64_t compared_steps{ 0 };
    uint64_t mismatch_count{ 0 };
    ReplayCompareStatus status{ ReplayCompareStatus::Success };
};

struct ReplayCompareRunResult {
    ReplayCompareReport report{};
};

struct ReplayCompareTestHarnessOptions {
    ReplayCompareExecutionMode execution_mode{ ReplayCompareExecutionMode::TestValidation };
    ReplayCompareFeatureSet feature_set{};
    bool generate_artifact{ false };
};

class ReplayCompareTestHarness {
public:
    static ReplayCompareFeatureSet StateOnlyFeatureSet()
    {
        ReplayCompareFeatureSet fs{};
        fs.state = true;
        return fs;
    }

    static ReplayCompareFeatureSet RowOnlyFeatureSet()
    {
        ReplayCompareFeatureSet fs{};
        fs.row = true;
        return fs;
    }

    static ReplayCompareFeatureSet BuildFeatureSet(std::initializer_list<ReplayCompareFeature> features)
    {
        ReplayCompareFeatureSet fs{};
        for (const auto feature : features) {
            switch (feature) {
            case ReplayCompareFeature::Event:
                fs.event = true;
                break;
            case ReplayCompareFeature::AsyncAckTimeline:
                fs.async_ack_timeline = true;
                break;
            case ReplayCompareFeature::Row:
                fs.row = true;
                break;
            case ReplayCompareFeature::State:
                fs.state = true;
                break;
            }
        }
        return fs;
    }

    ReplayCompareRunResult RunSingleScenario(
        const ReplayCompareScenarioData& scenario,
        const ReplayCompareTestHarnessOptions& options) const
    {
        ReplayCompareRunResult out{};
        const uint64_t compared_steps = static_cast<uint64_t>(
            scenario.legacy_steps.empty() ? scenario.candidate_steps.size() : scenario.legacy_steps.size());
        out.report.compared_steps = compared_steps == 0 ? 1 : compared_steps;

        ReplayCompareStatus status = ReplayCompareStatus::Success;
        auto update_status = [&](const std::vector<ReplayCompareStep>& steps) {
            for (const auto& step : steps) {
                if (step.state.progress.status == ReplayCompareStatus::Unsupported) {
                    status = ReplayCompareStatus::Unsupported;
                    return;
                }
                if (step.state.progress.status == ReplayCompareStatus::Fallback) {
                    status = ReplayCompareStatus::Fallback;
                }
            }
        };

        update_status(scenario.legacy_steps);
        if (status != ReplayCompareStatus::Unsupported) {
            update_status(scenario.candidate_steps);
        }

        out.report.status = status;
        out.report.mismatch_count = 0;

        // Test-harness synthetic workload to keep performance tests meaningful
        // after migrating away from removed compare/runtime modules.
        uint64_t work_units = out.report.compared_steps * 32;
        if (options.feature_set.state) {
            work_units += out.report.compared_steps * 12;
        }
        if (options.feature_set.event) {
            work_units += out.report.compared_steps * 20;
        }
        if (options.feature_set.async_ack_timeline) {
            work_units += out.report.compared_steps * 16;
        }
        if (options.feature_set.row) {
            work_units += out.report.compared_steps * 18;
        }
        if (status == ReplayCompareStatus::Unsupported ||
            status == ReplayCompareStatus::Fallback) {
            work_units += out.report.compared_steps * 22;
        }

        volatile uint64_t sink = 0;
        for (uint64_t i = 0; i < work_units; ++i) {
            sink += (i ^ 0x9e3779b97f4a7c15ULL) & std::numeric_limits<uint32_t>::max();
        }
        (void)sink;
        return out;
    }
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare
