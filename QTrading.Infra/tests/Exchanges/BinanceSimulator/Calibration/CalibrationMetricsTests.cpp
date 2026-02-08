#include <gtest/gtest.h>

#include "Exchanges/BinanceSimulator/Calibration/CalibrationMetrics.hpp"

using namespace QTrading::Infra::Exchanges::BinanceSim::Calibration;

TEST(CalibrationMetricsTest, EvaluateObjectiveComputesWeightedScore)
{
    MetricVector sim{};
    sim.fill_ratio = 0.55;
    sim.avg_slippage_bps = 12.0;
    sim.taker_ratio = 0.40;
    sim.funding_cost = -120.0;
    sim.tail_loss = -3000.0;

    MetricVector ref{};
    ref.fill_ratio = 0.50;
    ref.avg_slippage_bps = 10.0;
    ref.taker_ratio = 0.50;
    ref.funding_cost = -100.0;
    ref.tail_loss = -2500.0;

    ObjectiveWeights w{};
    w.fill_ratio_weight = 2.0;
    w.slippage_weight = 1.0;
    w.taker_ratio_weight = 1.5;
    w.funding_cost_weight = 0.5;
    w.tail_loss_weight = 0.5;

    const auto out = evaluate_objective(sim, ref, w);

    EXPECT_GT(out.fill_ratio_error_pct, 0.0);
    EXPECT_GT(out.avg_slippage_error_pct, 0.0);
    EXPECT_GT(out.taker_ratio_error_pct, 0.0);
    EXPECT_GT(out.funding_cost_error_pct, 0.0);
    EXPECT_GT(out.tail_loss_error_pct, 0.0);
    EXPECT_GT(out.weighted_score, out.fill_ratio_error_pct);
}

TEST(CalibrationMetricsTest, AcceptanceKpiGateUsesThresholds)
{
    MetricVector sim{};
    sim.fill_ratio = 0.54;         // +8%
    sim.avg_slippage_bps = 11.5;   // +15%
    sim.taker_ratio = 0.46;        // -8%

    MetricVector ref{};
    ref.fill_ratio = 0.50;
    ref.avg_slippage_bps = 10.0;
    ref.taker_ratio = 0.50;

    AcceptanceKpiThresholds th{};
    th.max_fill_ratio_diff_pct = 10.0;
    th.max_avg_slippage_diff_pct = 20.0;
    th.max_taker_ratio_diff_pct = 15.0;

    const auto pass = evaluate_acceptance_kpi(sim, ref, th);
    EXPECT_TRUE(pass.pass);

    sim.avg_slippage_bps = 13.0; // +30% > 20% threshold
    const auto fail = evaluate_acceptance_kpi(sim, ref, th);
    EXPECT_FALSE(fail.pass);
}

TEST(CalibrationMetricsTest, BuildWalkForwardWindowsCreatesRollingSplits)
{
    const auto windows = build_walk_forward_windows(
        /*total_points*/ 100,
        /*train_size*/ 40,
        /*validate_size*/ 10,
        /*step_size*/ 10);

    ASSERT_EQ(windows.size(), 6u);
    EXPECT_EQ(windows[0].train_begin, 0u);
    EXPECT_EQ(windows[0].train_end, 40u);
    EXPECT_EQ(windows[0].validate_begin, 40u);
    EXPECT_EQ(windows[0].validate_end, 50u);

    EXPECT_EQ(windows.back().train_begin, 50u);
    EXPECT_EQ(windows.back().train_end, 90u);
    EXPECT_EQ(windows.back().validate_begin, 90u);
    EXPECT_EQ(windows.back().validate_end, 100u);
}

TEST(CalibrationMetricsTest, EvaluateWalkForwardPipelineAggregatesWindowScores)
{
    std::vector<MetricVector> ref(6);
    std::vector<MetricVector> sim(6);
    for (size_t i = 0; i < 6; ++i) {
        ref[i] = MetricVector{ 0.50, 10.0, 0.50, -100.0, -2000.0 };
        sim[i] = ref[i];
    }

    // Make the second validation slice fail KPI thresholds.
    sim[4].fill_ratio = 0.70;
    sim[4].avg_slippage_bps = 14.0;
    sim[4].taker_ratio = 0.70;

    const std::vector<WalkForwardWindow> windows{
        WalkForwardWindow{ 0, 2, 2, 4 },
        WalkForwardWindow{ 1, 3, 3, 5 }
    };

    const auto pipeline = evaluate_walk_forward_pipeline(sim, ref, windows);

    ASSERT_EQ(pipeline.windows.size(), 2u);
    EXPECT_GT(pipeline.avg_validate_score, 0.0);
    EXPECT_DOUBLE_EQ(pipeline.validate_kpi_pass_rate, 0.5);
    EXPECT_TRUE(pipeline.windows[0].validate.kpi.pass);
    EXPECT_FALSE(pipeline.windows[1].validate.kpi.pass);
}

TEST(CalibrationMetricsTest, AcceptanceGateDecisionUsesMinPassRate)
{
    std::vector<MetricVector> ref(4);
    std::vector<MetricVector> sim(4);
    for (size_t i = 0; i < 4; ++i) {
        ref[i] = MetricVector{ 0.50, 10.0, 0.50, -100.0, -2000.0 };
        sim[i] = ref[i];
    }

    sim[3].avg_slippage_bps = 13.0;

    const std::vector<WalkForwardWindow> windows{
        WalkForwardWindow{ 0, 1, 1, 2 },
        WalkForwardWindow{ 1, 2, 2, 3 },
        WalkForwardWindow{ 2, 3, 3, 4 }
    };
    const auto pipeline = evaluate_walk_forward_pipeline(sim, ref, windows);
    const auto gate50 = evaluate_acceptance_gate(pipeline, 0.5);
    const auto gate75 = evaluate_acceptance_gate(pipeline, 0.75);

    EXPECT_EQ(gate50.total_windows, 3u);
    EXPECT_EQ(gate50.passed_windows, 2u);
    EXPECT_TRUE(gate50.pass);
    EXPECT_FALSE(gate75.pass);
}
