#include <gtest/gtest.h>

#include <vector>

#include "Exchanges/BinanceSimulator/Diagnostics/Compare/PerformanceCiGate.hpp"

namespace ReplayCompare = QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare;

TEST(PerformanceCiGateTests, BudgetExceedFailsFastAndReturnsFirstFailingMetric)
{
    const std::vector<ReplayCompare::PerformanceGateMetricCheck> checks = {
        { "hot-path", "account_ns_per_op", "account-hot-path", 100.0, 120.0, ReplayCompare::PerformanceGateComparison::LessOrEqual },
        { "hot-path", "session_p95_ns_per_step", "session-hot-path", 250.0, 200.0, ReplayCompare::PerformanceGateComparison::LessOrEqual },
        { "mixed", "throughput_ratio_vs_replay", "funding-reference-edge", 0.10, 0.20, ReplayCompare::PerformanceGateComparison::GreaterOrEqual },
    };
    ReplayCompare::PerformanceGatePolicy policy{};
    policy.fail_fast = true;

    const auto decision = ReplayCompare::PerformanceCiGate::Evaluate(checks, policy);
    EXPECT_FALSE(decision.pass);
    ASSERT_TRUE(decision.first_failing_metric.has_value());
    EXPECT_EQ(*decision.first_failing_metric, "session_p95_ns_per_step");
    ASSERT_EQ(decision.failure_lines.size(), 1u);
    EXPECT_NE(decision.failure_lines.front().find("PERF_GATE_FAIL"), std::string::npos);
}

TEST(PerformanceCiGateTests, WithinBudgetPassesAndProducesNoFailureLine)
{
    const std::vector<ReplayCompare::PerformanceGateMetricCheck> checks = {
        { "hot-path", "account_ns_per_op", "account-hot-path", 100.0, 120.0, ReplayCompare::PerformanceGateComparison::LessOrEqual },
        { "mixed", "throughput_ratio_vs_replay", "funding-reference-edge", 0.25, 0.20, ReplayCompare::PerformanceGateComparison::GreaterOrEqual },
    };
    ReplayCompare::PerformanceGatePolicy policy{};
    policy.fail_fast = true;

    const auto decision = ReplayCompare::PerformanceCiGate::Evaluate(checks, policy);
    EXPECT_TRUE(decision.pass);
    EXPECT_FALSE(decision.first_failing_metric.has_value());
    EXPECT_TRUE(decision.failure_lines.empty());
}

TEST(PerformanceCiGateTests, FailOutputContainsMetricAndScenarioForFastLocalization)
{
    const ReplayCompare::PerformanceGateMetricCheck check{
        "log-heavy",
        "publish_throughput",
        "dense-publish",
        12.5,
        25.0,
        ReplayCompare::PerformanceGateComparison::GreaterOrEqual
    };

    const auto line = ReplayCompare::PerformanceCiGate::FormatFailLine(check);
    EXPECT_NE(line.find("PERF_GATE_FAIL"), std::string::npos);
    EXPECT_NE(line.find("gate=log-heavy"), std::string::npos);
    EXPECT_NE(line.find("metric=publish_throughput"), std::string::npos);
    EXPECT_NE(line.find("scenario=dense-publish"), std::string::npos);
    EXPECT_NE(line.find("actual=12.5"), std::string::npos);
    EXPECT_NE(line.find("budget=25"), std::string::npos);
}

TEST(PerformanceCiGateTests, PerformanceGateCanCoexistWithReplayCompareGate)
{
    const std::vector<ReplayCompare::PerformanceGateMetricCheck> checks = {
        { "hot-path", "account_ns_per_op", "account-hot-path", 100.0, 120.0, ReplayCompare::PerformanceGateComparison::LessOrEqual },
    };
    ReplayCompare::PerformanceGatePolicy policy{};
    policy.fail_fast = true;
    policy.require_replay_compare_gate = true;

    ReplayCompare::ReplayCompareScenarioPackResult compare_result{};
    compare_result.total_mismatch_count = 0;
    compare_result.failed_scenarios = 0;
    compare_result.unsupported_scenarios = 1;
    compare_result.fallback_scenarios = 0;
    compare_result.all_passed = false;

    ReplayCompare::ReplayCompareGatePolicy compare_policy{};
    compare_policy.allow_unsupported_with_waiver = true;
    compare_policy.allow_fallback_with_waiver = true;
    compare_policy.fail_on_semantic_mismatch = true;
    compare_policy.soft_gate = false;

    const auto replay_decision = ReplayCompare::ReplayCompareCiGate::Evaluate(compare_result, compare_policy);
    ASSERT_TRUE(replay_decision.pass);
    ASSERT_TRUE(replay_decision.requires_waiver);

    const auto decision = ReplayCompare::PerformanceCiGate::EvaluateWithReplayCompare(checks, policy, replay_decision);
    EXPECT_TRUE(decision.pass);
    EXPECT_TRUE(decision.requires_waiver);
    EXPECT_FALSE(decision.first_failing_metric.has_value());
}
