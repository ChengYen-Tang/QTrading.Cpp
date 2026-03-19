#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "Exchanges/BinanceSimulator/Diagnostics/Compare/DifferentialReplayRunner.hpp"

namespace ReplayCompare = QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare;

namespace {

ReplayCompare::ReplayCoreInputBundle BuildInput(
    const std::string& name,
    uint64_t expected_steps)
{
    ReplayCompare::ReplayCoreInputBundle input{};
    input.scenario.name = name;
    input.scenario.dataset_id = "deterministic-replay-pack-v1";
    input.scenario.start_ts_exchange = 1000;
    input.scenario.end_ts_exchange = 1000 + expected_steps;
    input.scenario.expected_steps = expected_steps;
    input.run_id = 42;
    input.random_seed = 123456;
    input.config.feature_set = ReplayCompare::ReplayCompareFeatureSet::All();
    input.legacy_core_state = std::make_shared<int>(1);
    input.candidate_core_state = std::make_shared<int>(2);
    return input;
}

} // namespace

TEST(DifferentialReplayRunnerTests, ScenarioAndFeatureSetAreDeterministicAndConfigurable)
{
    auto input = BuildInput("single-symbol", 3);

    EXPECT_TRUE(input.scenario.IsDeterministic());
    EXPECT_TRUE(input.config.feature_set.Has(ReplayCompare::ReplayCompareFeature::State));
    EXPECT_TRUE(input.config.feature_set.Has(ReplayCompare::ReplayCompareFeature::Event));
    EXPECT_TRUE(input.config.feature_set.Has(ReplayCompare::ReplayCompareFeature::LogRow));
    EXPECT_TRUE(input.config.feature_set.Has(ReplayCompare::ReplayCompareFeature::AsyncAckTimeline));
}

TEST(DifferentialReplayRunnerTests, SuccessStatusUsesCompareCallbackForEachStep)
{
    auto input = BuildInput("success-path", 3);

    size_t legacy_calls = 0;
    size_t candidate_calls = 0;
    size_t compare_calls = 0;

    ReplayCompare::DifferentialReplayRunner::Callbacks callbacks{};
    callbacks.run_legacy_step = [&](const ReplayCompare::ReplayCoreInputBundle&, uint64_t) {
        ++legacy_calls;
    };
    callbacks.run_candidate_step = [&](const ReplayCompare::ReplayCoreInputBundle&, uint64_t) {
        ++candidate_calls;
    };
    callbacks.compare_step = [&](const ReplayCompare::ReplayCoreInputBundle&, uint64_t step_index) {
        ++compare_calls;
        ReplayCompare::ReplayStepCompareResult step{};
        step.step_index = step_index;
        step.status = ReplayCompare::ReplayCompareStatus::Success;
        step.compared = true;
        step.matched = true;
        return step;
    };

    ReplayCompare::DifferentialReplayRunner runner;
    const auto report = runner.Run(input, callbacks);

    EXPECT_EQ(report.status, ReplayCompare::ReplayCompareStatus::Success);
    EXPECT_TRUE(report.legacy_baseline_preserved);
    EXPECT_EQ(report.steps.size(), 3u);
    EXPECT_EQ(report.mismatch_count, 0u);
    EXPECT_EQ(report.legacy_steps_executed, 3u);
    EXPECT_EQ(report.candidate_steps_executed, 3u);
    EXPECT_EQ(report.compared_steps, 3u);
    EXPECT_EQ(legacy_calls, 3u);
    EXPECT_EQ(candidate_calls, 3u);
    EXPECT_EQ(compare_calls, 3u);
}

TEST(DifferentialReplayRunnerTests, FailedStatusCapturesFirstMismatchAndStopsByDefault)
{
    auto input = BuildInput("mismatch-path", 5);

    size_t legacy_calls = 0;
    size_t candidate_calls = 0;

    ReplayCompare::DifferentialReplayRunner::Callbacks callbacks{};
    callbacks.run_legacy_step = [&](const ReplayCompare::ReplayCoreInputBundle&, uint64_t) {
        ++legacy_calls;
    };
    callbacks.run_candidate_step = [&](const ReplayCompare::ReplayCoreInputBundle&, uint64_t) {
        ++candidate_calls;
    };
    callbacks.compare_step = [&](const ReplayCompare::ReplayCoreInputBundle&, uint64_t step_index) {
        ReplayCompare::ReplayStepCompareResult step{};
        step.step_index = step_index;
        step.status = ReplayCompare::ReplayCompareStatus::Success;
        step.compared = true;
        step.matched = true;

        if (step_index == 1) {
            step.status = ReplayCompare::ReplayCompareStatus::Failed;
            step.matched = false;
            ReplayCompare::ReplayMismatch mismatch{};
            mismatch.domain = ReplayCompare::ReplayMismatchDomain::State;
            mismatch.step_index = step_index;
            mismatch.field = "wallet_balance";
            mismatch.legacy_value = "1000.0";
            mismatch.candidate_value = "999.5";
            mismatch.reason = "state drift";
            step.mismatches.push_back(std::move(mismatch));
        }
        return step;
    };

    ReplayCompare::DifferentialReplayRunner runner;
    const auto report = runner.Run(input, callbacks);

    EXPECT_EQ(report.status, ReplayCompare::ReplayCompareStatus::Failed);
    EXPECT_TRUE(report.legacy_baseline_preserved);
    EXPECT_EQ(report.steps.size(), 2u);
    EXPECT_EQ(report.mismatch_count, 1u);
    ASSERT_TRUE(report.first_mismatch.has_value());
    EXPECT_EQ(report.first_mismatch->step_index, 1u);
    EXPECT_EQ(report.first_mismatch->field, "wallet_balance");
    ASSERT_TRUE(report.first_divergent_step.has_value());
    EXPECT_EQ(*report.first_divergent_step, 1u);
    ASSERT_TRUE(report.first_divergent_status.has_value());
    EXPECT_EQ(*report.first_divergent_status, ReplayCompare::ReplayCompareStatus::Failed);
    EXPECT_EQ(legacy_calls, 2u);
    EXPECT_EQ(candidate_calls, 2u);
}

TEST(DifferentialReplayRunnerTests, UnsupportedStatusRequiresIndependentCoreStates)
{
    auto input = BuildInput("independent-state", 2);

    auto shared_state = std::make_shared<int>(7);
    input.legacy_core_state = shared_state;
    input.candidate_core_state = shared_state;

    ReplayCompare::DifferentialReplayRunner::Callbacks callbacks{};
    callbacks.run_legacy_step = [](const ReplayCompare::ReplayCoreInputBundle&, uint64_t) {};
    callbacks.run_candidate_step = [](const ReplayCompare::ReplayCoreInputBundle&, uint64_t) {};
    callbacks.compare_step = [](const ReplayCompare::ReplayCoreInputBundle&, uint64_t step_index) {
        ReplayCompare::ReplayStepCompareResult step{};
        step.step_index = step_index;
        step.status = ReplayCompare::ReplayCompareStatus::Success;
        step.compared = true;
        return step;
    };

    ReplayCompare::DifferentialReplayRunner runner;
    const auto report = runner.Run(input, callbacks);

    EXPECT_EQ(report.status, ReplayCompare::ReplayCompareStatus::Unsupported);
    EXPECT_EQ(report.steps.size(), 0u);
    EXPECT_NE(report.summary.find("independent"), std::string::npos);
}

TEST(DifferentialReplayRunnerTests, FallbackStatusPreservesLegacyBaselineWhenCandidateFails)
{
    auto input = BuildInput("candidate-failure", 3);
    input.config.stop_on_first_mismatch = false;

    size_t legacy_calls = 0;
    size_t candidate_calls = 0;

    ReplayCompare::DifferentialReplayRunner::Callbacks callbacks{};
    callbacks.run_legacy_step = [&](const ReplayCompare::ReplayCoreInputBundle&, uint64_t) {
        ++legacy_calls;
    };
    callbacks.run_candidate_step = [&](const ReplayCompare::ReplayCoreInputBundle&, uint64_t) {
        ++candidate_calls;
        throw std::runtime_error("candidate path unavailable");
    };
    callbacks.compare_step = [](const ReplayCompare::ReplayCoreInputBundle&, uint64_t step_index) {
        ReplayCompare::ReplayStepCompareResult step{};
        step.step_index = step_index;
        step.status = ReplayCompare::ReplayCompareStatus::Success;
        step.compared = true;
        return step;
    };

    ReplayCompare::DifferentialReplayRunner runner;
    const auto report = runner.Run(input, callbacks);

    EXPECT_EQ(report.status, ReplayCompare::ReplayCompareStatus::Fallback);
    EXPECT_TRUE(report.legacy_baseline_preserved);
    EXPECT_EQ(report.legacy_steps_executed, 3u);
    EXPECT_EQ(report.candidate_steps_executed, 0u);
    EXPECT_EQ(legacy_calls, 3u);
    EXPECT_EQ(candidate_calls, 3u);
    ASSERT_EQ(report.steps.size(), 3u);
    for (const auto& step : report.steps) {
        EXPECT_EQ(step.status, ReplayCompare::ReplayCompareStatus::Fallback);
        EXPECT_TRUE(step.fallback_to_legacy);
    }
    ASSERT_TRUE(report.first_divergent_status.has_value());
    EXPECT_EQ(*report.first_divergent_status, ReplayCompare::ReplayCompareStatus::Fallback);
    ASSERT_TRUE(report.first_divergent_step.has_value());
    EXPECT_EQ(*report.first_divergent_step, 0u);
}
