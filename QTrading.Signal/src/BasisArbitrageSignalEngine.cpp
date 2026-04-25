#include "Signal/BasisArbitrageSignalEngine.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace QTrading::Signal {

namespace {

constexpr double kHoursPerYear = 365.0 * 24.0;
constexpr uint64_t kFundingSettlementIntervalMs = 8ull * 60ull * 60ull * 1000ull;

struct RollingStats {
    double mean{ 0.0 };
    double stddev{ 0.0 };
    double z{ 0.0 };
    double abs_z{ 0.0 };
};

std::optional<RollingStats> ComputeRollingStats(
    const std::deque<double>& window,
    double current,
    double std_floor)
{
    if (window.empty()) {
        return std::nullopt;
    }

    const double n = static_cast<double>(window.size());
    const double mean = std::accumulate(window.begin(), window.end(), 0.0) / n;
    double var = 0.0;
    for (const double v : window) {
        const double d = v - mean;
        var += d * d;
    }
    var /= n;

    RollingStats out;
    out.mean = mean;
    out.stddev = std::max(std::sqrt(std::max(var, 0.0)), std_floor);
    out.z = (current - out.mean) / out.stddev;
    out.abs_z = std::fabs(out.z);
    return out;
}

double EmaAlphaFromPeriod(std::size_t period)
{
    const double p = static_cast<double>(std::max<std::size_t>(period, 1));
    return 2.0 / (p + 1.0);
}

void UpdateEma(double value, double alpha, bool& initialized, double& ema)
{
    if (!initialized) {
        ema = value;
        return;
    }
    ema = ema * (1.0 - alpha) + value * alpha;
}

template <typename T>
void PushBounded(std::deque<T>& window, T value, std::size_t max_size)
{
    if (max_size == 0) {
        window.clear();
        return;
    }
    window.push_back(value);
    while (window.size() > max_size) {
        window.pop_front();
    }
}

} // namespace

BasisArbitrageSignalEngine::BasisArbitrageSignalEngine(Config cfg)
    : cfg_(std::move(cfg))
{
    cfg_.basis_mr_window_bars = std::max<std::size_t>(cfg_.basis_mr_window_bars, 10);
    cfg_.basis_mr_min_samples = std::max<std::size_t>(cfg_.basis_mr_min_samples, 10);
    cfg_.basis_mr_min_samples = std::min(cfg_.basis_mr_min_samples, cfg_.basis_mr_window_bars);
    if (!std::isfinite(cfg_.basis_mr_entry_z) || cfg_.basis_mr_entry_z < 0.0) {
        cfg_.basis_mr_entry_z = 1.5;
    }
    if (!std::isfinite(cfg_.basis_mr_exit_z) || cfg_.basis_mr_exit_z < 0.0) {
        cfg_.basis_mr_exit_z = 0.6;
    }
    if (cfg_.basis_mr_exit_z > cfg_.basis_mr_entry_z) {
        cfg_.basis_mr_exit_z = cfg_.basis_mr_entry_z;
    }
    if (!std::isfinite(cfg_.basis_mr_max_abs_z) || cfg_.basis_mr_max_abs_z <= 0.0) {
        cfg_.basis_mr_max_abs_z = std::max(6.0, cfg_.basis_mr_entry_z);
    }
    if (cfg_.basis_mr_max_abs_z < cfg_.basis_mr_entry_z) {
        cfg_.basis_mr_max_abs_z = cfg_.basis_mr_entry_z;
    }
    cfg_.basis_mr_entry_persistence_bars =
        std::max(cfg_.basis_mr_entry_persistence_bars, 1u);
    cfg_.basis_mr_exit_persistence_bars =
        std::max(cfg_.basis_mr_exit_persistence_bars, 1u);
    if (!std::isfinite(cfg_.basis_mr_std_floor) || cfg_.basis_mr_std_floor <= 0.0) {
        cfg_.basis_mr_std_floor = 1e-6;
    }
    if (!std::isfinite(cfg_.basis_mr_confidence_floor)) {
        cfg_.basis_mr_confidence_floor = 0.35;
    }
    cfg_.basis_mr_confidence_floor = std::clamp(cfg_.basis_mr_confidence_floor, 0.0, 1.0);

    cfg_.basis_regime_window_bars = std::max<std::size_t>(cfg_.basis_regime_window_bars, 10);
    cfg_.basis_regime_min_samples =
        std::max<std::size_t>(cfg_.basis_regime_min_samples, 10);
    cfg_.basis_regime_min_samples =
        std::min(cfg_.basis_regime_min_samples, cfg_.basis_regime_window_bars);
    if (!std::isfinite(cfg_.basis_regime_calm_z) || cfg_.basis_regime_calm_z < 0.0) {
        cfg_.basis_regime_calm_z = 1.0;
    }
    if (!std::isfinite(cfg_.basis_regime_stress_z) ||
        cfg_.basis_regime_stress_z < cfg_.basis_regime_calm_z)
    {
        cfg_.basis_regime_stress_z = cfg_.basis_regime_calm_z + 1.0;
    }
    if (!std::isfinite(cfg_.basis_regime_min_confidence_scale)) {
        cfg_.basis_regime_min_confidence_scale = 0.5;
    }
    cfg_.basis_regime_min_confidence_scale =
        std::clamp(cfg_.basis_regime_min_confidence_scale, 0.0, 1.0);
    if (!std::isfinite(cfg_.basis_stop_alpha_z) || cfg_.basis_stop_alpha_z < 0.0) {
        cfg_.basis_stop_alpha_z = 0.0;
    }
    if (!std::isfinite(cfg_.basis_stop_risk_z) || cfg_.basis_stop_risk_z < 0.0) {
        cfg_.basis_stop_risk_z = 0.0;
    }
    if (!std::isfinite(cfg_.basis_cost_edge_threshold_pct) || cfg_.basis_cost_edge_threshold_pct < 0.0) {
        cfg_.basis_cost_edge_threshold_pct = 0.0;
    }
    if (!std::isfinite(cfg_.basis_cost_expected_hold_hours) || cfg_.basis_cost_expected_hold_hours < 0.0) {
        cfg_.basis_cost_expected_hold_hours = 0.0;
    }
    if (!std::isfinite(cfg_.basis_cost_expected_funding_settlements) ||
        cfg_.basis_cost_expected_funding_settlements < 0.0)
    {
        cfg_.basis_cost_expected_funding_settlements = 0.0;
    }
    if (!std::isfinite(cfg_.basis_cost_borrow_apr) || cfg_.basis_cost_borrow_apr < 0.0) {
        cfg_.basis_cost_borrow_apr = 0.0;
    }
    if (!std::isfinite(cfg_.basis_cost_trading_cost_rate_per_leg) ||
        cfg_.basis_cost_trading_cost_rate_per_leg < 0.0)
    {
        cfg_.basis_cost_trading_cost_rate_per_leg = 0.0;
    }
    if (!std::isfinite(cfg_.basis_cost_risk_penalty_weight) || cfg_.basis_cost_risk_penalty_weight < 0.0) {
        cfg_.basis_cost_risk_penalty_weight = 0.0;
    }
    if (!std::isfinite(cfg_.basis_cost_trend_penalty_weight) || cfg_.basis_cost_trend_penalty_weight < 0.0) {
        cfg_.basis_cost_trend_penalty_weight = 0.0;
    }
    cfg_.basis_cost_common_move_penalty_window_bars =
        std::max<std::size_t>(cfg_.basis_cost_common_move_penalty_window_bars, 2);
    if (!std::isfinite(cfg_.basis_cost_common_move_penalty_start_pct) ||
        cfg_.basis_cost_common_move_penalty_start_pct < 0.0)
    {
        cfg_.basis_cost_common_move_penalty_start_pct = 0.0;
    }
    if (!std::isfinite(cfg_.basis_cost_common_move_penalty_full_pct) ||
        cfg_.basis_cost_common_move_penalty_full_pct < cfg_.basis_cost_common_move_penalty_start_pct)
    {
        cfg_.basis_cost_common_move_penalty_full_pct =
            cfg_.basis_cost_common_move_penalty_start_pct;
    }
    if (!std::isfinite(cfg_.basis_cost_common_move_penalty_min_scale)) {
        cfg_.basis_cost_common_move_penalty_min_scale = 0.20;
    }
    cfg_.basis_cost_common_move_penalty_min_scale =
        std::clamp(cfg_.basis_cost_common_move_penalty_min_scale, 0.0, 1.0);
}

SignalDecision BasisArbitrageSignalEngine::on_market(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    SignalDecision out{};
    out.strategy = "basis_arbitrage";
    out.strategy_kind = QTrading::Contracts::StrategyKind::BasisArbitrage;
    if (!market) {
        return out;
    }

    out.ts_ms = market->Timestamp;
    out.symbol = cfg_.perp_symbol;
    out.allocator_score = 0.0;

    if (!ResolveSymbolIds(market)) {
        out.status = SignalStatus::Inactive;
        return out;
    }

    const auto trade_basis_pct_opt = ComputeBasisPct(market, false);
    if (!trade_basis_pct_opt.has_value() || !std::isfinite(*trade_basis_pct_opt)) {
        out.status = SignalStatus::Inactive;
        return out;
    }
    const double trade_basis_pct = *trade_basis_pct_opt;
    if (symbol_ids_.spot_id >= market->trade_klines_by_id.size() ||
        symbol_ids_.perp_id >= market->trade_klines_by_id.size() ||
        !market->trade_klines_by_id[symbol_ids_.spot_id].has_value() ||
        !market->trade_klines_by_id[symbol_ids_.perp_id].has_value())
    {
        out.status = SignalStatus::Inactive;
        return out;
    }
    const double spot_trade_close =
        market->trade_klines_by_id[symbol_ids_.spot_id]->ClosePrice;
    const double perp_trade_close =
        market->trade_klines_by_id[symbol_ids_.perp_id]->ClosePrice;
    PushBounded(
        spot_trade_close_window_,
        spot_trade_close,
        cfg_.basis_cost_common_move_penalty_window_bars);
    PushBounded(
        perp_trade_close_window_,
        perp_trade_close,
        cfg_.basis_cost_common_move_penalty_window_bars);

    const std::size_t ema_long_period = std::max<std::size_t>(cfg_.basis_mr_window_bars, 3);
    const std::size_t ema_mid_period = std::max<std::size_t>(ema_long_period / 3, 2);
    const std::size_t ema_short_period = std::max<std::size_t>(ema_long_period / 8, 2);
    UpdateEma(
        trade_basis_pct,
        EmaAlphaFromPeriod(ema_short_period),
        alpha_ema_initialized_,
        basis_alpha_ema_short_);
    UpdateEma(
        trade_basis_pct,
        EmaAlphaFromPeriod(ema_mid_period),
        alpha_ema_initialized_,
        basis_alpha_ema_mid_);
    UpdateEma(
        trade_basis_pct,
        EmaAlphaFromPeriod(ema_long_period),
        alpha_ema_initialized_,
        basis_alpha_ema_long_);
    alpha_ema_initialized_ = true;

    const auto risk_basis_pct_opt = ComputeBasisPct(market, true);
    const auto regime_basis_pct_opt = ComputeBasisPct(market, cfg_.basis_regime_use_mark_index);
    std::optional<double> mark_index_bps = std::nullopt;
    if (risk_basis_pct_opt.has_value() && std::isfinite(*risk_basis_pct_opt)) {
        mark_index_bps = *risk_basis_pct_opt * 10000.0;
    }
    if (regime_basis_pct_opt.has_value() && std::isfinite(*regime_basis_pct_opt)) {
        basis_regime_window_.push_back(*regime_basis_pct_opt);
        while (basis_regime_window_.size() > cfg_.basis_regime_window_bars) {
            basis_regime_window_.pop_front();
        }
    }

    std::optional<RollingStats> alpha_stats = std::nullopt;
    const auto risk_stats =
        (regime_basis_pct_opt.has_value() && std::isfinite(*regime_basis_pct_opt))
        ? ComputeRollingStats(basis_regime_window_, *regime_basis_pct_opt, cfg_.basis_mr_std_floor)
        : std::nullopt;
    const double risk_basis =
        (regime_basis_pct_opt.has_value() && risk_stats.has_value() && risk_stats->stddev > 0.0)
        ? std::fabs(*regime_basis_pct_opt) / risk_stats->stddev
        : 0.0;

    const bool mark_index_hard_breached =
        (cfg_.mark_index_hard_exit_bps > 0.0) &&
        mark_index_bps.has_value() &&
        (std::abs(*mark_index_bps) >= cfg_.mark_index_hard_exit_bps);

    out.status = SignalStatus::Active;
    out.confidence = 1.0;
    out.strategy = "basis_arbitrage";
    if (cfg_.basis_mr_enabled) {
        auto mr_basis_pct_opt = ComputeBasisPct(market, cfg_.basis_mr_use_mark_index);
        if (!mr_basis_pct_opt.has_value() || !std::isfinite(*mr_basis_pct_opt)) {
            mr_active_ = false;
            mr_entry_streak_ = 0;
            mr_exit_streak_ = 0;
            out.status = SignalStatus::Inactive;
            out.confidence = 0.0;
            return out;
        }

        basis_window_.push_back(*mr_basis_pct_opt);
        while (basis_window_.size() > cfg_.basis_mr_window_bars) {
            basis_window_.pop_front();
        }
        alpha_stats = ComputeRollingStats(basis_window_, *mr_basis_pct_opt, cfg_.basis_mr_std_floor);

        if (basis_window_.size() < cfg_.basis_mr_min_samples) {
            mr_active_ = false;
            mr_entry_streak_ = 0;
            mr_exit_streak_ = 0;
            out.status = SignalStatus::Inactive;
            out.confidence = 0.0;
            return out;
        }

        const auto mr_stats =
            ComputeRollingStats(basis_window_, *mr_basis_pct_opt, cfg_.basis_mr_std_floor);
        if (!mr_stats.has_value()) {
            out.status = SignalStatus::Inactive;
            out.confidence = 0.0;
            return out;
        }
        const double z = mr_stats->z;
        const double abs_z = mr_stats->abs_z;

        const bool entry_candidate =
            (abs_z >= cfg_.basis_mr_entry_z && abs_z <= cfg_.basis_mr_max_abs_z);
        const bool exit_candidate =
            (abs_z <= cfg_.basis_mr_exit_z || abs_z >= cfg_.basis_mr_max_abs_z);

        if (!mr_active_) {
            const bool cooldown_ok =
                (cfg_.basis_mr_cooldown_ms == 0) ||
                (mr_last_exit_ts_ == 0) ||
                (out.ts_ms >= mr_last_exit_ts_ + cfg_.basis_mr_cooldown_ms);
            if (cooldown_ok && entry_candidate) {
                mr_entry_streak_ += 1;
            }
            else {
                mr_entry_streak_ = 0;
            }
            if (mr_entry_streak_ >= cfg_.basis_mr_entry_persistence_bars) {
                mr_active_ = true;
                mr_entry_streak_ = 0;
                mr_exit_streak_ = 0;
            }
        }
        else {
            if (exit_candidate) {
                mr_exit_streak_ += 1;
            }
            else {
                mr_exit_streak_ = 0;
            }
            if (mr_exit_streak_ >= cfg_.basis_mr_exit_persistence_bars) {
                mr_active_ = false;
                mr_last_exit_ts_ = out.ts_ms;
                mr_exit_streak_ = 0;
                mr_entry_streak_ = 0;
            }
        }

        const bool base_allows_active = (out.status == SignalStatus::Active);
        const bool final_active = base_allows_active && mr_active_;
        out.status = final_active ? SignalStatus::Active : SignalStatus::Inactive;
        if (!final_active) {
            out.confidence = 0.0;
            return out;
        }

        if (cfg_.basis_mr_confidence_from_z) {
            const double span = std::max(cfg_.basis_mr_max_abs_z - cfg_.basis_mr_entry_z, 1e-12);
            const double progress = std::clamp((abs_z - cfg_.basis_mr_entry_z) / span, 0.0, 1.0);
            const double z_scale =
                cfg_.basis_mr_confidence_floor +
                (1.0 - cfg_.basis_mr_confidence_floor) * progress;
            out.confidence = std::clamp(out.confidence * z_scale, 0.0, 1.0);
        }
    }

    const bool alpha_stop_breached =
        (cfg_.basis_stop_alpha_z > 0.0) &&
        alpha_stats.has_value() &&
        (basis_window_.size() >= cfg_.basis_mr_min_samples) &&
        (alpha_stats->abs_z >= cfg_.basis_stop_alpha_z);
    const bool risk_stop_breached =
        (cfg_.basis_stop_risk_z > 0.0) &&
        risk_stats.has_value() &&
        (basis_regime_window_.size() >= cfg_.basis_regime_min_samples) &&
        (risk_stats->abs_z >= cfg_.basis_stop_risk_z);

    if (alpha_stop_breached || risk_stop_breached) {
        mr_active_ = false;
        mr_entry_streak_ = 0;
        mr_exit_streak_ = 0;
        mr_last_exit_ts_ = out.ts_ms;
        out.status = SignalStatus::Inactive;
        out.confidence = 0.0;
        return out;
    }

    if (mark_index_hard_breached) {
        out.status = SignalStatus::Inactive;
        out.confidence = 0.0;
        return out;
    }

    if (cfg_.basis_regime_confidence_enabled &&
        out.status == SignalStatus::Active &&
        out.confidence > 0.0)
    {
        if (!alpha_stats.has_value()) {
            alpha_stats = ComputeRollingStats(basis_window_, trade_basis_pct, cfg_.basis_mr_std_floor);
        }
        if (risk_stats.has_value() &&
            basis_regime_window_.size() >= cfg_.basis_regime_min_samples)
        {
            const double alpha_abs_z = alpha_stats.has_value() ? alpha_stats->abs_z : 0.0;
            const double risk_abs_z = risk_stats->abs_z;
            const bool stress_regime =
                (risk_abs_z >= cfg_.basis_regime_stress_z) ||
                (risk_basis >= cfg_.basis_regime_stress_z);
            const bool calm_regime =
                (alpha_abs_z <= cfg_.basis_regime_calm_z) &&
                (risk_abs_z <= cfg_.basis_regime_calm_z) &&
                (risk_basis <= cfg_.basis_regime_calm_z);

            double confidence_scale = 1.0;
            if (!calm_regime) {
                const double z_denom =
                    std::max(cfg_.basis_regime_stress_z - cfg_.basis_regime_calm_z, 1e-12);
                const double z_progress =
                    std::clamp((risk_abs_z - cfg_.basis_regime_calm_z) / z_denom, 0.0, 1.0);
                const double risk_progress =
                    std::clamp((risk_basis - cfg_.basis_regime_calm_z) / z_denom, 0.0, 1.0);
                const double progress = stress_regime
                    ? std::max(z_progress, risk_progress)
                    : 0.5 * std::max(z_progress, risk_progress);
                confidence_scale =
                    1.0 - (1.0 - cfg_.basis_regime_min_confidence_scale) * progress;
            }

            const double ema_dispersion =
                std::fabs(basis_alpha_ema_short_ - basis_alpha_ema_mid_) +
                std::fabs(basis_alpha_ema_mid_ - basis_alpha_ema_long_);
            if (alpha_stats.has_value() &&
                alpha_stats->stddev > 0.0 &&
                std::signbit(trade_basis_pct) == std::signbit(basis_alpha_ema_short_ - basis_alpha_ema_long_))
            {
                const double trend_pressure =
                    std::clamp(ema_dispersion / alpha_stats->stddev, 0.0, 1.0);
                confidence_scale *= (1.0 - 0.15 * trend_pressure);
            }

            out.confidence = std::clamp(out.confidence * confidence_scale, 0.0, 1.0);
        }
    }

    if (cfg_.basis_cost_gate_enabled &&
        out.status == SignalStatus::Active &&
        out.confidence > 0.0 &&
        alpha_stats.has_value())
    {
        const double dislocation_abs =
            std::max(0.0, std::fabs(trade_basis_pct - alpha_stats->mean));
        const double exit_band_gain =
            alpha_stats->stddev * std::max(0.0, alpha_stats->abs_z - cfg_.basis_mr_exit_z);
        const double expected_reversion_gain =
            std::max(0.0, std::min(dislocation_abs, exit_band_gain));
        const double borrow_cost =
            cfg_.basis_cost_borrow_apr * (cfg_.basis_cost_expected_hold_hours / kHoursPerYear);
        const double trading_cost =
            2.0 * cfg_.basis_cost_trading_cost_rate_per_leg;
        const double ema_dispersion =
            std::fabs(basis_alpha_ema_short_ - basis_alpha_ema_mid_) +
            std::fabs(basis_alpha_ema_mid_ - basis_alpha_ema_long_);
        const double normalized_trend_pressure =
            (alpha_stats->stddev > 0.0)
            ? std::clamp(ema_dispersion / alpha_stats->stddev, 0.0, 1.0)
            : 0.0;
        const double risk_penalty =
            cfg_.basis_cost_risk_penalty_weight * risk_basis +
            cfg_.basis_cost_trend_penalty_weight * normalized_trend_pressure;

        double funding_edge = 0.0;
        if (cfg_.basis_cost_include_funding &&
            symbol_ids_.perp_id < market->funding_by_id.size())
        {
            const auto& funding_opt = market->funding_by_id[symbol_ids_.perp_id];
            if (funding_opt.has_value() && std::isfinite(funding_opt->Rate)) {
                double funding_capture_scale = 0.0;
                const double expected_hold_ms =
                    std::max(0.0, cfg_.basis_cost_expected_hold_hours * 60.0 * 60.0 * 1000.0);
                if (expected_hold_ms > 0.0) {
                    const uint64_t elapsed_since_settlement_ms =
                        (out.ts_ms >= funding_opt->FundingTime)
                        ? (out.ts_ms - funding_opt->FundingTime)
                        : 0ull;
                    const uint64_t capped_elapsed_ms =
                        std::min<uint64_t>(elapsed_since_settlement_ms, kFundingSettlementIntervalMs);
                    const double time_to_next_settlement_ms =
                        static_cast<double>(kFundingSettlementIntervalMs - capped_elapsed_ms);
                    funding_capture_scale =
                        std::clamp(
                            (expected_hold_ms - time_to_next_settlement_ms) / expected_hold_ms,
                            0.0,
                            1.0);
                }
                const double direction = (trade_basis_pct >= 0.0) ? 1.0 : -1.0;
                funding_edge =
                    direction *
                    funding_opt->Rate *
                    cfg_.basis_cost_expected_funding_settlements *
                    funding_capture_scale;
            }
        }

        const double net_edge =
            expected_reversion_gain +
            funding_edge -
            borrow_cost -
            trading_cost -
            risk_penalty;

        if (net_edge <= cfg_.basis_cost_edge_threshold_pct) {
            mr_active_ = false;
            mr_entry_streak_ = 0;
            mr_exit_streak_ = 0;
            mr_last_exit_ts_ = out.ts_ms;
            out.status = SignalStatus::Inactive;
            out.confidence = 0.0;
            return out;
        }

        const double gross_edge =
            std::max(
                expected_reversion_gain + std::max(0.0, funding_edge),
                cfg_.basis_cost_edge_threshold_pct + 1e-12);
        const double edge_scale = std::clamp(net_edge / gross_edge, 0.0, 1.0);
        out.confidence = std::clamp(out.confidence * edge_scale, 0.0, 1.0);

        const double score_cost_base =
            std::max(
                trading_cost + borrow_cost + risk_penalty,
                cfg_.basis_cost_edge_threshold_pct + 1e-12);
        const double net_margin =
            std::max(0.0, net_edge - cfg_.basis_cost_edge_threshold_pct);
        const double cost_efficiency =
            std::clamp(net_margin / score_cost_base, 0.0, 2.0) * 0.5;
        const double capture_efficiency =
            (dislocation_abs > 0.0)
            ? std::clamp(expected_reversion_gain / dislocation_abs, 0.0, 1.0)
            : 0.0;
        const double z_span =
            std::max(cfg_.basis_mr_max_abs_z - cfg_.basis_mr_entry_z, 1e-12);
        const double z_intensity =
            cfg_.basis_mr_enabled
            ? std::clamp((alpha_stats->abs_z - cfg_.basis_mr_entry_z) / z_span, 0.0, 1.0)
            : 1.0;
        const double z_freshness =
            cfg_.basis_mr_enabled
            ? std::clamp(alpha_stats->abs_z / std::max(cfg_.basis_mr_entry_z, 1e-12), 0.0, 1.0)
            : 1.0;
        double common_move_scale = 1.0;
        double common_move_abs = 0.0;
        bool common_move_same_direction = false;
        if (spot_trade_close_window_.size() >= cfg_.basis_cost_common_move_penalty_window_bars &&
            perp_trade_close_window_.size() >= cfg_.basis_cost_common_move_penalty_window_bars)
        {
            const double spot_base =
                std::max(std::fabs(spot_trade_close_window_.front()), 1e-12);
            const double perp_base =
                std::max(std::fabs(perp_trade_close_window_.front()), 1e-12);
            const double spot_return = (spot_trade_close / spot_base) - 1.0;
            const double perp_return = (perp_trade_close / perp_base) - 1.0;
            common_move_same_direction = (std::signbit(spot_return) == std::signbit(perp_return));
            if (common_move_same_direction) {
                common_move_abs =
                    0.5 * (std::fabs(spot_return) + std::fabs(perp_return));
                if (common_move_abs > cfg_.basis_cost_common_move_penalty_start_pct) {
                    const double span =
                        std::max(
                            cfg_.basis_cost_common_move_penalty_full_pct -
                            cfg_.basis_cost_common_move_penalty_start_pct,
                            1e-12);
                    const double progress =
                        std::clamp(
                            (common_move_abs - cfg_.basis_cost_common_move_penalty_start_pct) / span,
                            0.0,
                            1.0);
                    common_move_scale =
                        1.0 -
                        (1.0 - cfg_.basis_cost_common_move_penalty_min_scale) * progress;
                }
            }
        }
        if (common_move_same_direction &&
            cfg_.basis_cost_common_move_penalty_full_pct > 0.0 &&
            common_move_abs >= cfg_.basis_cost_common_move_penalty_full_pct)
        {
            mr_active_ = false;
            mr_entry_streak_ = 0;
            mr_exit_streak_ = 0;
            mr_last_exit_ts_ = out.ts_ms;
            out.status = SignalStatus::Inactive;
            out.confidence = 0.0;
            return out;
        }
        const double trend_efficiency =
            std::clamp(1.0 - normalized_trend_pressure, 0.0, 1.0);
        out.confidence = std::clamp(out.confidence * common_move_scale, 0.0, 1.0);
        const double score_quality =
            std::max(0.0, cost_efficiency) *
            std::max(0.0, capture_efficiency) *
            std::max(0.0, z_freshness) *
            std::max(0.0, common_move_scale) *
            std::max(0.0, 0.25 + 0.75 * z_intensity) *
            std::max(0.0, 0.25 + 0.75 * trend_efficiency);
        out.allocator_score =
            std::max(0.0, net_edge * out.confidence * score_quality);
    }
    else if (alpha_stats.has_value()) {
        out.allocator_score =
            std::max(0.0, std::fabs(trade_basis_pct - alpha_stats->mean)) * out.confidence;
    }

    out.allocator_score = std::max(0.0, out.allocator_score);

    if (out.status == SignalStatus::Active &&
        out.confidence > 0.0 &&
        mark_index_bps.has_value() &&
        cfg_.mark_index_soft_derisk_start_bps > 0.0 &&
        cfg_.mark_index_soft_derisk_full_bps >= cfg_.mark_index_soft_derisk_start_bps)
    {
        const double abs_bps = std::abs(*mark_index_bps);
        double derisk_scale = 1.0;
        if (abs_bps > cfg_.mark_index_soft_derisk_start_bps) {
            if (cfg_.mark_index_soft_derisk_full_bps <= cfg_.mark_index_soft_derisk_start_bps + 1e-12) {
                derisk_scale = cfg_.mark_index_soft_derisk_min_confidence_scale;
            }
            else {
                const double progress = std::clamp(
                    (abs_bps - cfg_.mark_index_soft_derisk_start_bps) /
                    (cfg_.mark_index_soft_derisk_full_bps - cfg_.mark_index_soft_derisk_start_bps),
                    0.0,
                    1.0);
                derisk_scale =
                    1.0 - progress * (1.0 - cfg_.mark_index_soft_derisk_min_confidence_scale);
            }
        }
        out.confidence = std::clamp(out.confidence * derisk_scale, 0.0, 1.0);
    }

    out.urgency = SignalUrgency::Low;

    return out;
}

bool BasisArbitrageSignalEngine::ResolveSymbolIds(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    return QTrading::Signal::Support::ResolvePairSymbolIds(
        market,
        cfg_.spot_symbol,
        cfg_.perp_symbol,
        symbol_ids_);
}

std::optional<double> BasisArbitrageSignalEngine::ComputeBasisPct(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market,
    bool use_mark_index)
{
    if (!ResolveSymbolIds(market)) {
        return std::nullopt;
    }
    return QTrading::Signal::Support::ComputeBasisPct(market, symbol_ids_, use_mark_index);
}

} // namespace QTrading::Signal
