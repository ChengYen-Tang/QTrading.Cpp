#pragma once

#include <cstddef>
#include <vector>

namespace QTrading::Utils::Calibration {

struct MetricVector {
    double fill_ratio{ 0.0 };
    double avg_slippage_bps{ 0.0 };
    double taker_ratio{ 0.0 };
    double funding_cost{ 0.0 };
    double tail_loss{ 0.0 };
};

struct ObjectiveWeights {
    double fill_ratio_weight{ 1.0 };
    double slippage_weight{ 1.0 };
    double taker_ratio_weight{ 1.0 };
    double funding_cost_weight{ 1.0 };
    double tail_loss_weight{ 1.0 };
};

struct AcceptanceKpiThresholds {
    double max_fill_ratio_diff_pct{ 10.0 };
    double max_avg_slippage_diff_pct{ 20.0 };
    double max_taker_ratio_diff_pct{ 15.0 };
};

struct AcceptanceKpiResult {
    double fill_ratio_diff_pct{ 0.0 };
    double avg_slippage_diff_pct{ 0.0 };
    double taker_ratio_diff_pct{ 0.0 };
    bool pass{ false };
};

struct ObjectiveResult {
    double fill_ratio_error_pct{ 0.0 };
    double avg_slippage_error_pct{ 0.0 };
    double taker_ratio_error_pct{ 0.0 };
    double funding_cost_error_pct{ 0.0 };
    double tail_loss_error_pct{ 0.0 };
    double weighted_score{ 0.0 };
    AcceptanceKpiResult kpi{};
};

struct WalkForwardWindow {
    size_t train_begin{ 0 };
    size_t train_end{ 0 };
    size_t validate_begin{ 0 };
    size_t validate_end{ 0 };
};

struct WindowCalibrationResult {
    WalkForwardWindow window{};
    ObjectiveResult train{};
    ObjectiveResult validate{};
};

struct WalkForwardCalibrationResult {
    std::vector<WindowCalibrationResult> windows{};
    double avg_train_score{ 0.0 };
    double avg_validate_score{ 0.0 };
    double validate_kpi_pass_rate{ 0.0 };
};

struct AcceptanceGateDecision {
    size_t total_windows{ 0 };
    size_t passed_windows{ 0 };
    double observed_pass_rate{ 0.0 };
    double min_required_pass_rate{ 1.0 };
    bool pass{ false };
};

ObjectiveResult evaluate_objective(
    const MetricVector& simulated,
    const MetricVector& reference,
    const ObjectiveWeights& weights = {},
    const AcceptanceKpiThresholds& thresholds = {});

AcceptanceKpiResult evaluate_acceptance_kpi(
    const MetricVector& simulated,
    const MetricVector& reference,
    const AcceptanceKpiThresholds& thresholds = {});

std::vector<WalkForwardWindow> build_walk_forward_windows(
    size_t total_points,
    size_t train_size,
    size_t validate_size,
    size_t step_size);

WalkForwardCalibrationResult evaluate_walk_forward_pipeline(
    const std::vector<MetricVector>& simulated_series,
    const std::vector<MetricVector>& reference_series,
    const std::vector<WalkForwardWindow>& windows,
    const ObjectiveWeights& weights = {},
    const AcceptanceKpiThresholds& thresholds = {});

AcceptanceGateDecision evaluate_acceptance_gate(
    const WalkForwardCalibrationResult& pipeline,
    double min_required_pass_rate = 1.0);

} // namespace QTrading::Utils::Calibration
