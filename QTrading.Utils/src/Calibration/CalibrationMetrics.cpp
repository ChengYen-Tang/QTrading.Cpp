#include "Calibration/CalibrationMetrics.hpp"

#include <algorithm>
#include <cmath>

namespace QTrading::Utils::Calibration {

namespace {

double relative_error_pct(double actual, double target)
{
    const double denom = std::max(1e-12, std::abs(target));
    return std::abs(actual - target) / denom * 100.0;
}

struct MetricPrefixSums {
    std::vector<double> fill_ratio;
    std::vector<double> avg_slippage_bps;
    std::vector<double> taker_ratio;
    std::vector<double> funding_cost;
    std::vector<double> tail_loss;
};

MetricPrefixSums build_prefix_sums(const std::vector<MetricVector>& series, size_t count)
{
    MetricPrefixSums prefix{};
    prefix.fill_ratio.resize(count + 1, 0.0);
    prefix.avg_slippage_bps.resize(count + 1, 0.0);
    prefix.taker_ratio.resize(count + 1, 0.0);
    prefix.funding_cost.resize(count + 1, 0.0);
    prefix.tail_loss.resize(count + 1, 0.0);

    for (size_t i = 0; i < count; ++i) {
        prefix.fill_ratio[i + 1] = prefix.fill_ratio[i] + series[i].fill_ratio;
        prefix.avg_slippage_bps[i + 1] = prefix.avg_slippage_bps[i] + series[i].avg_slippage_bps;
        prefix.taker_ratio[i + 1] = prefix.taker_ratio[i] + series[i].taker_ratio;
        prefix.funding_cost[i + 1] = prefix.funding_cost[i] + series[i].funding_cost;
        prefix.tail_loss[i + 1] = prefix.tail_loss[i] + series[i].tail_loss;
    }

    return prefix;
}

MetricVector average_slice(
    const MetricPrefixSums& prefix,
    size_t begin,
    size_t end,
    size_t total_count)
{
    MetricVector out{};
    if (begin >= end || begin >= total_count) {
        return out;
    }

    const size_t clamped_end = std::min(end, total_count);
    if (begin >= clamped_end) {
        return out;
    }

    const size_t count = clamped_end - begin;
    const double denom = static_cast<double>(count);
    out.fill_ratio = (prefix.fill_ratio[clamped_end] - prefix.fill_ratio[begin]) / denom;
    out.avg_slippage_bps = (prefix.avg_slippage_bps[clamped_end] - prefix.avg_slippage_bps[begin]) / denom;
    out.taker_ratio = (prefix.taker_ratio[clamped_end] - prefix.taker_ratio[begin]) / denom;
    out.funding_cost = (prefix.funding_cost[clamped_end] - prefix.funding_cost[begin]) / denom;
    out.tail_loss = (prefix.tail_loss[clamped_end] - prefix.tail_loss[begin]) / denom;
    return out;
}

} // namespace

AcceptanceKpiResult evaluate_acceptance_kpi(
    const MetricVector& simulated,
    const MetricVector& reference,
    const AcceptanceKpiThresholds& thresholds)
{
    AcceptanceKpiResult out{};
    out.fill_ratio_diff_pct = relative_error_pct(simulated.fill_ratio, reference.fill_ratio);
    out.avg_slippage_diff_pct = relative_error_pct(simulated.avg_slippage_bps, reference.avg_slippage_bps);
    out.taker_ratio_diff_pct = relative_error_pct(simulated.taker_ratio, reference.taker_ratio);
    out.pass =
        out.fill_ratio_diff_pct <= thresholds.max_fill_ratio_diff_pct &&
        out.avg_slippage_diff_pct <= thresholds.max_avg_slippage_diff_pct &&
        out.taker_ratio_diff_pct <= thresholds.max_taker_ratio_diff_pct;
    return out;
}

ObjectiveResult evaluate_objective(
    const MetricVector& simulated,
    const MetricVector& reference,
    const ObjectiveWeights& weights,
    const AcceptanceKpiThresholds& thresholds)
{
    ObjectiveResult out{};
    out.fill_ratio_error_pct = relative_error_pct(simulated.fill_ratio, reference.fill_ratio);
    out.avg_slippage_error_pct = relative_error_pct(simulated.avg_slippage_bps, reference.avg_slippage_bps);
    out.taker_ratio_error_pct = relative_error_pct(simulated.taker_ratio, reference.taker_ratio);
    out.funding_cost_error_pct = relative_error_pct(simulated.funding_cost, reference.funding_cost);
    out.tail_loss_error_pct = relative_error_pct(simulated.tail_loss, reference.tail_loss);

    out.weighted_score =
        weights.fill_ratio_weight * out.fill_ratio_error_pct +
        weights.slippage_weight * out.avg_slippage_error_pct +
        weights.taker_ratio_weight * out.taker_ratio_error_pct +
        weights.funding_cost_weight * out.funding_cost_error_pct +
        weights.tail_loss_weight * out.tail_loss_error_pct;

    out.kpi = evaluate_acceptance_kpi(simulated, reference, thresholds);
    return out;
}

std::vector<WalkForwardWindow> build_walk_forward_windows(
    size_t total_points,
    size_t train_size,
    size_t validate_size,
    size_t step_size)
{
    std::vector<WalkForwardWindow> windows;
    if (train_size == 0 || validate_size == 0 || step_size == 0) {
        return windows;
    }
    if (train_size + validate_size > total_points) {
        return windows;
    }

    for (size_t train_begin = 0;; train_begin += step_size) {
        const size_t train_end = train_begin + train_size;
        const size_t validate_begin = train_end;
        const size_t validate_end = validate_begin + validate_size;
        if (validate_end > total_points) {
            break;
        }
        windows.push_back(WalkForwardWindow{
            train_begin,
            train_end,
            validate_begin,
            validate_end
            });
    }

    return windows;
}

WalkForwardCalibrationResult evaluate_walk_forward_pipeline(
    const std::vector<MetricVector>& simulated_series,
    const std::vector<MetricVector>& reference_series,
    const std::vector<WalkForwardWindow>& windows,
    const ObjectiveWeights& weights,
    const AcceptanceKpiThresholds& thresholds)
{
    WalkForwardCalibrationResult out{};
    const size_t total_points = std::min(simulated_series.size(), reference_series.size());
    if (total_points == 0 || windows.empty()) {
        return out;
    }

    const auto sim_prefix = build_prefix_sums(simulated_series, total_points);
    const auto ref_prefix = build_prefix_sums(reference_series, total_points);

    size_t passed = 0;
    out.windows.reserve(windows.size());
    for (const auto& window : windows) {
        if (window.train_begin >= window.train_end ||
            window.validate_begin >= window.validate_end ||
            window.validate_end > total_points ||
            window.train_end > total_points) {
            continue;
        }

        const MetricVector train_sim = average_slice(sim_prefix, window.train_begin, window.train_end, total_points);
        const MetricVector train_ref = average_slice(ref_prefix, window.train_begin, window.train_end, total_points);
        const MetricVector validate_sim = average_slice(sim_prefix, window.validate_begin, window.validate_end, total_points);
        const MetricVector validate_ref = average_slice(ref_prefix, window.validate_begin, window.validate_end, total_points);

        WindowCalibrationResult row{};
        row.window = window;
        row.train = evaluate_objective(train_sim, train_ref, weights, thresholds);
        row.validate = evaluate_objective(validate_sim, validate_ref, weights, thresholds);
        if (row.validate.kpi.pass) {
            ++passed;
        }
        out.avg_train_score += row.train.weighted_score;
        out.avg_validate_score += row.validate.weighted_score;
        out.windows.push_back(row);
    }

    if (!out.windows.empty()) {
        const double denom = static_cast<double>(out.windows.size());
        out.avg_train_score /= denom;
        out.avg_validate_score /= denom;
        out.validate_kpi_pass_rate = static_cast<double>(passed) / denom;
    }

    return out;
}

AcceptanceGateDecision evaluate_acceptance_gate(
    const WalkForwardCalibrationResult& pipeline,
    double min_required_pass_rate)
{
    AcceptanceGateDecision out{};
    out.total_windows = pipeline.windows.size();
    out.min_required_pass_rate = std::clamp(min_required_pass_rate, 0.0, 1.0);

    for (const auto& row : pipeline.windows) {
        if (row.validate.kpi.pass) {
            ++out.passed_windows;
        }
    }

    if (out.total_windows > 0) {
        out.observed_pass_rate =
            static_cast<double>(out.passed_windows) /
            static_cast<double>(out.total_windows);
    }
    out.pass = out.observed_pass_rate >= out.min_required_pass_rate;
    return out;
}

} // namespace QTrading::Utils::Calibration
