#include "Signal/FundingCarrySignalEngine.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace QTrading::Signal {
namespace {
constexpr double kBasisToFundingScale = 0.25;
constexpr double kFundingProxyEmaAlpha = 0.10;
constexpr double kGateEpsilon = 1e-12;
constexpr double kDefaultFundingConfidenceScale = 0.00005;

double Clamp01(double value)
{
    if (!std::isfinite(value)) {
        return 0.0;
    }
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

double ClampAlpha(double value, double fallback)
{
    if (!std::isfinite(value)) {
        return fallback;
    }
    return Clamp01(value);
}

double ClampNonNegative(double value)
{
    if (!std::isfinite(value) || value < 0.0) {
        return 0.0;
    }
    return value;
}

double ClampPositive(double value, double fallback)
{
    if (!std::isfinite(value) || value <= 0.0) {
        return fallback;
    }
    return value;
}

double ClampUnitOpenClosed(double value, double fallback)
{
    if (!std::isfinite(value) || value <= 0.0) {
        return fallback;
    }
    return std::min(value, 1.0);
}

std::size_t ClampMinSamples(std::size_t value)
{
    return std::max<std::size_t>(value, 1);
}

double ClampQuantile(double q)
{
    if (!std::isfinite(q)) {
        return 0.5;
    }
    return std::clamp(q, 0.0, 1.0);
}

void PushBounded(std::deque<double>& values, double v, std::size_t max_size)
{
    if (!std::isfinite(v) || max_size == 0) {
        return;
    }
    values.push_back(v);
    while (values.size() > max_size) {
        values.pop_front();
    }
}

void PushBounded(std::deque<int>& values, int v, std::size_t max_size)
{
    if (max_size == 0) {
        return;
    }
    values.push_back(v);
    while (values.size() > max_size) {
        values.pop_front();
    }
}

std::optional<double> QuantileFromDeque(const std::deque<double>& values, double quantile)
{
    if (values.empty()) {
        return std::nullopt;
    }

    std::vector<double> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    const double q = ClampQuantile(quantile);
    const std::size_t idx = static_cast<std::size_t>(
        std::llround(q * static_cast<double>(sorted.size() - 1)));
    return sorted[idx];
}

double FundingConfidenceScale()
{
    static const double value = std::max(kDefaultFundingConfidenceScale, kGateEpsilon);
    return value;
}

double NormalizedFundingScore(const QTrading::Signal::FundingCarrySignalEngine::Config& cfg,
    double funding_proxy_ema,
    bool funding_gate_enabled)
{
    if (!funding_gate_enabled) {
        // When funding gates are disabled, still expose funding regime quality
        // so Risk can scale carry size continuously instead of hard on/off.
        const double normalized = std::tanh(funding_proxy_ema / FundingConfidenceScale());
        return Clamp01(0.5 + 0.5 * normalized);
    }

    const double low = std::min(cfg.exit_min_funding_rate, cfg.entry_min_funding_rate);
    const double high = std::max(cfg.exit_min_funding_rate, cfg.entry_min_funding_rate);
    const double width = std::max(high - low, kGateEpsilon);
    return Clamp01((funding_proxy_ema - low) / width);
}

double NormalizedBasisScore(const QTrading::Signal::FundingCarrySignalEngine::Config& cfg,
    double basis_pct,
    bool basis_gate_enabled)
{
    const double basis_abs = std::abs(basis_pct);
    if (!basis_gate_enabled) {
        // Mild penalty when no explicit gate is configured.
        return 1.0 / (1.0 + basis_abs * 100.0);
    }

    const double tight = std::min(cfg.entry_max_basis_pct, cfg.exit_max_basis_pct);
    const double loose = std::max(cfg.entry_max_basis_pct, cfg.exit_max_basis_pct);
    const double width = std::max(loose - tight, kGateEpsilon);
    const double normalized = Clamp01((basis_abs - tight) / width);
    return 1.0 - normalized;
}

std::optional<double> SameSignPersistenceRatio(const std::deque<int>& signs)
{
    if (signs.size() < 2) {
        return std::nullopt;
    }
    std::size_t same_sign_pairs = 0;
    std::size_t eligible_pairs = 0;
    for (std::size_t i = 1; i < signs.size(); ++i) {
        const int prev = signs[i - 1];
        const int cur = signs[i];
        if (prev == 0 || cur == 0) {
            continue;
        }
        eligible_pairs += 1;
        if (prev == cur) {
            same_sign_pairs += 1;
        }
    }
    if (eligible_pairs == 0) {
        return std::nullopt;
    }
    return static_cast<double>(same_sign_pairs) / static_cast<double>(eligible_pairs);
}

std::optional<double> NegativeShareRatio(const std::deque<int>& signs)
{
    if (signs.empty()) {
        return std::nullopt;
    }
    std::size_t negative_count = 0;
    std::size_t eligible = 0;
    for (const int sign : signs) {
        if (sign == 0) {
            continue;
        }
        eligible += 1;
        if (sign < 0) {
            negative_count += 1;
        }
    }
    if (eligible == 0) {
        return std::nullopt;
    }
    return static_cast<double>(negative_count) / static_cast<double>(eligible);
}

std::size_t TrailingNegativeRun(const std::deque<int>& signs)
{
    std::size_t run = 0;
    for (auto it = signs.rbegin(); it != signs.rend(); ++it) {
        if (*it < 0) {
            run += 1;
            continue;
        }
        break;
    }
    return run;
}
}

FundingCarrySignalEngine::FundingCarrySignalEngine(Config cfg)
    : cfg_(std::move(cfg))
{
    cfg_.entry_persistence_settlements = std::max(cfg_.entry_persistence_settlements, 1u);
    cfg_.exit_persistence_settlements = std::max(cfg_.exit_persistence_settlements, 1u);
    cfg_.hard_negative_persistence_settlements = std::max(cfg_.hard_negative_persistence_settlements, 1u);
    cfg_.adaptive_funding_window_settlements = ClampMinSamples(cfg_.adaptive_funding_window_settlements);
    cfg_.adaptive_funding_min_samples = ClampMinSamples(cfg_.adaptive_funding_min_samples);
    cfg_.adaptive_funding_entry_quantile = ClampQuantile(cfg_.adaptive_funding_entry_quantile);
    cfg_.adaptive_funding_exit_quantile = ClampQuantile(cfg_.adaptive_funding_exit_quantile);
    if (cfg_.adaptive_funding_exit_quantile > cfg_.adaptive_funding_entry_quantile) {
        cfg_.adaptive_funding_exit_quantile = cfg_.adaptive_funding_entry_quantile;
    }
    cfg_.adaptive_basis_window_bars = ClampMinSamples(cfg_.adaptive_basis_window_bars);
    cfg_.adaptive_basis_min_samples = ClampMinSamples(cfg_.adaptive_basis_min_samples);
    cfg_.adaptive_basis_entry_quantile = ClampQuantile(cfg_.adaptive_basis_entry_quantile);
    cfg_.adaptive_basis_exit_quantile = ClampQuantile(cfg_.adaptive_basis_exit_quantile);
    if (cfg_.adaptive_basis_exit_quantile < cfg_.adaptive_basis_entry_quantile) {
        cfg_.adaptive_basis_exit_quantile = cfg_.adaptive_basis_entry_quantile;
    }
    if (!std::isfinite(cfg_.adaptive_basis_floor_pct) || cfg_.adaptive_basis_floor_pct < 0.0) {
        cfg_.adaptive_basis_floor_pct = 0.0;
    }
    if (!std::isfinite(cfg_.adaptive_funding_entry_floor_rate) || cfg_.adaptive_funding_entry_floor_rate < 0.0) {
        cfg_.adaptive_funding_entry_floor_rate = 0.0;
    }
    if (!std::isfinite(cfg_.adaptive_funding_entry_cap_rate) || cfg_.adaptive_funding_entry_cap_rate < 0.0) {
        cfg_.adaptive_funding_entry_cap_rate = cfg_.adaptive_funding_entry_floor_rate;
    }
    if (cfg_.adaptive_funding_entry_cap_rate < cfg_.adaptive_funding_entry_floor_rate) {
        cfg_.adaptive_funding_entry_cap_rate = cfg_.adaptive_funding_entry_floor_rate;
    }
    if (!std::isfinite(cfg_.adaptive_funding_exit_ratio) || cfg_.adaptive_funding_exit_ratio < 0.0) {
        cfg_.adaptive_funding_exit_ratio = 0.30;
    }
    if (cfg_.adaptive_funding_exit_ratio > 1.0) {
        cfg_.adaptive_funding_exit_ratio = 1.0;
    }
    if (!std::isfinite(cfg_.inactivity_watchdog_min_rate)) {
        cfg_.inactivity_watchdog_min_rate = 0.0;
    }
    cfg_.inactivity_watchdog_min_confidence = Clamp01(cfg_.inactivity_watchdog_min_confidence);
    cfg_.adaptive_regime_min_samples = ClampMinSamples(cfg_.adaptive_regime_min_samples);
    cfg_.adaptive_regime_sign_window_settlements =
        ClampMinSamples(cfg_.adaptive_regime_sign_window_settlements);
    cfg_.adaptive_regime_sign_persist_high = Clamp01(cfg_.adaptive_regime_sign_persist_high);
    cfg_.adaptive_regime_sign_persist_low = Clamp01(cfg_.adaptive_regime_sign_persist_low);
    if (cfg_.adaptive_regime_sign_persist_low > cfg_.adaptive_regime_sign_persist_high) {
        cfg_.adaptive_regime_sign_persist_low = cfg_.adaptive_regime_sign_persist_high;
    }
    cfg_.adaptive_regime_entry_persistence_low =
        std::max(cfg_.adaptive_regime_entry_persistence_low, 1u);
    cfg_.adaptive_regime_entry_persistence_mid =
        std::max(cfg_.adaptive_regime_entry_persistence_mid, 1u);
    cfg_.adaptive_regime_entry_persistence_high =
        std::max(cfg_.adaptive_regime_entry_persistence_high, 1u);
    cfg_.adaptive_regime_exit_persistence_low =
        std::max(cfg_.adaptive_regime_exit_persistence_low, 1u);
    cfg_.adaptive_regime_exit_persistence_mid =
        std::max(cfg_.adaptive_regime_exit_persistence_mid, 1u);
    cfg_.adaptive_regime_exit_persistence_high =
        std::max(cfg_.adaptive_regime_exit_persistence_high, 1u);
    cfg_.adaptive_confidence_min_samples = ClampMinSamples(cfg_.adaptive_confidence_min_samples);
    cfg_.adaptive_confidence_low_quantile = ClampQuantile(cfg_.adaptive_confidence_low_quantile);
    cfg_.adaptive_confidence_high_quantile = ClampQuantile(cfg_.adaptive_confidence_high_quantile);
    if (cfg_.adaptive_confidence_high_quantile < cfg_.adaptive_confidence_low_quantile) {
        cfg_.adaptive_confidence_high_quantile = cfg_.adaptive_confidence_low_quantile;
    }
    cfg_.adaptive_confidence_floor = Clamp01(cfg_.adaptive_confidence_floor);
    if (!std::isfinite(cfg_.adaptive_confidence_ceiling)) {
        cfg_.adaptive_confidence_ceiling = 1.0;
    }
    cfg_.adaptive_confidence_ceiling = std::max(cfg_.adaptive_confidence_ceiling, cfg_.adaptive_confidence_floor);
    cfg_.adaptive_confidence_ema_alpha = ClampAlpha(cfg_.adaptive_confidence_ema_alpha, 0.35);
    if (!std::isfinite(cfg_.adaptive_confidence_bucket_step) || cfg_.adaptive_confidence_bucket_step < 0.0) {
        cfg_.adaptive_confidence_bucket_step = 0.0;
    }
    cfg_.adaptive_structure_min_samples = ClampMinSamples(cfg_.adaptive_structure_min_samples);
    cfg_.adaptive_structure_neg_share_weight = ClampNonNegative(cfg_.adaptive_structure_neg_share_weight);
    cfg_.adaptive_structure_neg_run_weight = ClampNonNegative(cfg_.adaptive_structure_neg_run_weight);
    cfg_.adaptive_structure_neg_run_norm = ClampPositive(cfg_.adaptive_structure_neg_run_norm, 12.0);
    cfg_.adaptive_structure_floor = Clamp01(cfg_.adaptive_structure_floor);
    if (!std::isfinite(cfg_.adaptive_structure_ceiling)) {
        cfg_.adaptive_structure_ceiling = 1.0;
    }
    cfg_.adaptive_structure_ceiling = std::max(
        cfg_.adaptive_structure_ceiling,
        cfg_.adaptive_structure_floor);
    cfg_.adaptive_structure_ema_alpha = ClampAlpha(cfg_.adaptive_structure_ema_alpha, 0.25);
    if (!std::isfinite(cfg_.adaptive_structure_bucket_step) || cfg_.adaptive_structure_bucket_step < 0.0) {
        cfg_.adaptive_structure_bucket_step = 0.0;
    }
    cfg_.mark_index_soft_derisk_start_bps = ClampNonNegative(cfg_.mark_index_soft_derisk_start_bps);
    cfg_.mark_index_soft_derisk_full_bps = ClampNonNegative(cfg_.mark_index_soft_derisk_full_bps);
    if (cfg_.mark_index_soft_derisk_full_bps < cfg_.mark_index_soft_derisk_start_bps) {
        cfg_.mark_index_soft_derisk_full_bps = cfg_.mark_index_soft_derisk_start_bps;
    }
    cfg_.mark_index_soft_derisk_min_confidence_scale = ClampUnitOpenClosed(
        cfg_.mark_index_soft_derisk_min_confidence_scale,
        0.30);
    cfg_.mark_index_hard_exit_bps = ClampNonNegative(cfg_.mark_index_hard_exit_bps);
    if (cfg_.funding_nowcast_interval_ms == 0) {
        cfg_.funding_nowcast_interval_ms = 8ull * 60ull * 60ull * 1000ull;
    }
    if (!std::isfinite(cfg_.pre_settlement_negative_exit_threshold)) {
        cfg_.pre_settlement_negative_exit_threshold = -1.0;
    }
    if (cfg_.pre_settlement_negative_exit_lookahead_ms > cfg_.funding_nowcast_interval_ms) {
        cfg_.pre_settlement_negative_exit_lookahead_ms = cfg_.funding_nowcast_interval_ms;
    }
    adaptive_funding_entry_min_cached_rate_ = cfg_.entry_min_funding_rate;
    adaptive_funding_exit_min_cached_rate_ = cfg_.exit_min_funding_rate;
    adaptive_basis_entry_max_cached_pct_ = cfg_.entry_max_basis_pct;
    adaptive_basis_exit_max_cached_pct_ = cfg_.exit_max_basis_pct;
    adaptive_regime_entry_persistence_cached_ = cfg_.entry_persistence_settlements;
    adaptive_regime_exit_persistence_cached_ = cfg_.exit_persistence_settlements;
    adaptive_confidence_multiplier_ = 1.0;
    adaptive_structure_multiplier_ = 1.0;
}

bool FundingCarrySignalEngine::market_has_symbols(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    if (!market) {
        return false;
    }

    if (!has_symbol_ids_ && market->symbols) {
        const auto& symbols = *market->symbols;
        for (std::size_t i = 0; i < symbols.size(); ++i) {
            if (symbols[i] == cfg_.spot_symbol) {
                spot_id_ = i;
            }
            if (symbols[i] == cfg_.perp_symbol) {
                perp_id_ = i;
            }
        }
        has_symbol_ids_ = (spot_id_ < symbols.size() && perp_id_ < symbols.size());
    }

    if (has_symbol_ids_ &&
        spot_id_ < market->trade_klines_by_id.size() &&
        perp_id_ < market->trade_klines_by_id.size())
    {
        return market->trade_klines_by_id[spot_id_].has_value() &&
            market->trade_klines_by_id[perp_id_].has_value();
    }

    return false;
}

SignalDecision FundingCarrySignalEngine::on_market(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    SignalDecision out;
    if (!market) {
        return out;
    }

    // Funding carry uses a delta-neutral structure; the signal is a readiness check
    // plus guardrails (funding/basis) to avoid unfavorable regimes.
    out.ts_ms = market->Timestamp;
    out.symbol = cfg_.perp_symbol;
    out.strategy = "funding_carry";

    if (!market_has_symbols(market)) {
        out.status = SignalStatus::Inactive;
        return out;
    }

    std::optional<double> spot_close;
    std::optional<double> perp_close;
    if (has_symbol_ids_ &&
        spot_id_ < market->trade_klines_by_id.size() &&
        perp_id_ < market->trade_klines_by_id.size())
    {
        const auto& spot_opt = market->trade_klines_by_id[spot_id_];
        const auto& perp_opt = market->trade_klines_by_id[perp_id_];
        if (spot_opt.has_value()) {
            spot_close = spot_opt->ClosePrice;
        }
        if (perp_opt.has_value()) {
            perp_close = perp_opt->ClosePrice;
        }
    }

    if (!spot_close.has_value() || !perp_close.has_value()) {
        out.status = SignalStatus::Inactive;
        return out;
    }

    if (*spot_close <= 0.0) {
        out.status = SignalStatus::Inactive;
        return out;
    }

    std::optional<double> perp_mark_price;
    std::optional<double> perp_index_price;
    if (has_symbol_ids_ && perp_id_ < market->mark_klines_by_id.size()) {
        const auto& mark_opt = market->mark_klines_by_id[perp_id_];
        if (mark_opt.has_value() && std::isfinite(mark_opt->ClosePrice) && mark_opt->ClosePrice > 0.0) {
            perp_mark_price = mark_opt->ClosePrice;
        }
    }
    if (has_symbol_ids_ && perp_id_ < market->index_klines_by_id.size()) {
        const auto& index_opt = market->index_klines_by_id[perp_id_];
        if (index_opt.has_value() && std::isfinite(index_opt->ClosePrice) && index_opt->ClosePrice > 0.0) {
            perp_index_price = index_opt->ClosePrice;
        }
    }
    std::optional<double> mark_index_bps;
    if (perp_mark_price.has_value() &&
        perp_index_price.has_value() &&
        std::abs(*perp_index_price) > kGateEpsilon)
    {
        mark_index_bps = ((*perp_mark_price - *perp_index_price) / *perp_index_price) * 10000.0;
    }
    const bool mark_index_hard_guard_enabled = cfg_.mark_index_hard_exit_bps > 0.0;
    const bool mark_index_hard_breached =
        mark_index_hard_guard_enabled &&
        mark_index_bps.has_value() &&
        std::abs(*mark_index_bps) >= cfg_.mark_index_hard_exit_bps;

    const double basis_pct = (*perp_close - *spot_close) / *spot_close;
    const double funding_proxy = basis_pct * kBasisToFundingScale;
    std::optional<double> latest_observed_funding_rate;
    std::optional<uint64_t> latest_observed_funding_time;
    if (has_symbol_ids_ &&
        perp_id_ < market->funding_by_id.size() &&
        market->funding_by_id[perp_id_].has_value())
    {
        const auto& funding = *market->funding_by_id[perp_id_];
        latest_observed_funding_rate = funding.Rate;
        latest_observed_funding_time = funding.FundingTime;
    }
    bool observed_funding_valid = false;
    bool settlement_advanced = false;
    double funding_settlement_signal = funding_proxy;
    std::optional<double> funding_settlement_rate;
    std::optional<uint64_t> funding_settlement_time;
    if (latest_observed_funding_rate.has_value() && latest_observed_funding_time.has_value()) {
        const auto funding_time = *latest_observed_funding_time;
        const bool not_future = funding_time <= out.ts_ms;
        const bool within_age = not_future && (
            !cfg_.observed_funding_max_age_ms ||
            ((out.ts_ms - funding_time) <= cfg_.observed_funding_max_age_ms));
        observed_funding_valid = not_future && within_age;
        if (observed_funding_valid) {
            funding_settlement_signal = *latest_observed_funding_rate;
            funding_settlement_rate = *latest_observed_funding_rate;
            funding_settlement_time = funding_time;
            settlement_advanced =
                (!has_last_observed_funding_time_) ||
                (funding_time != last_observed_funding_time_);
            if (settlement_advanced) {
                has_last_observed_funding_time_ = true;
                last_observed_funding_time_ = funding_time;
            }
        }
    }
    double funding_nowcast_signal = funding_settlement_signal;
    if (cfg_.funding_nowcast_enabled &&
        observed_funding_valid &&
        funding_settlement_rate.has_value() &&
        funding_settlement_time.has_value() &&
        cfg_.funding_nowcast_interval_ms > 0 &&
        out.ts_ms >= *funding_settlement_time)
    {
        const uint64_t elapsed_ms = out.ts_ms - *funding_settlement_time;
        const double progress = Clamp01(
            static_cast<double>(elapsed_ms) /
            static_cast<double>(cfg_.funding_nowcast_interval_ms));
        const double nowcast_rate =
            *funding_settlement_rate + (funding_proxy - *funding_settlement_rate) * progress;
        funding_nowcast_signal = nowcast_rate;
    }
    const bool use_nowcast_for_confidence =
        cfg_.funding_nowcast_enabled && cfg_.funding_nowcast_use_for_confidence;
    const double funding_confidence_signal =
        use_nowcast_for_confidence ? funding_nowcast_signal : funding_settlement_signal;

    const bool update_funding_ema = use_nowcast_for_confidence
        ? true
        : (!cfg_.lock_funding_to_settlement || !observed_funding_valid || settlement_advanced);
    if (!funding_proxy_initialized_) {
        funding_proxy_ema_ = funding_confidence_signal;
        funding_proxy_initialized_ = true;
    }
    else if (update_funding_ema) {
        funding_proxy_ema_ =
            funding_proxy_ema_ * (1.0 - kFundingProxyEmaAlpha) +
            funding_confidence_signal * kFundingProxyEmaAlpha;
    }

    double entry_min_funding_rate = cfg_.entry_min_funding_rate;
    double exit_min_funding_rate = cfg_.exit_min_funding_rate;
    double entry_max_basis_pct = cfg_.entry_max_basis_pct;
    double exit_max_basis_pct = cfg_.exit_max_basis_pct;
    const bool static_basis_gate_configured =
        (cfg_.entry_max_basis_pct < 1.0 - kGateEpsilon) ||
        (cfg_.exit_max_basis_pct < 1.0 - kGateEpsilon);

    if (cfg_.adaptive_gate_enabled ||
        cfg_.adaptive_confidence_enabled ||
        cfg_.adaptive_funding_soft_gate_enabled)
    {
        if (static_basis_gate_configured) {
            PushBounded(basis_abs_history_, std::abs(basis_pct), cfg_.adaptive_basis_window_bars);
        }
        if (observed_funding_valid && settlement_advanced) {
            PushBounded(
                funding_settlement_history_,
                funding_settlement_signal,
                cfg_.adaptive_funding_window_settlements);
        }
    }

    if ((cfg_.adaptive_regime_enabled || cfg_.adaptive_structure_enabled) &&
        observed_funding_valid &&
        settlement_advanced)
    {
        const int sign = (funding_settlement_signal > kGateEpsilon)
            ? 1
            : ((funding_settlement_signal < -kGateEpsilon) ? -1 : 0);
        PushBounded(
            funding_settlement_sign_history_,
            sign,
            cfg_.adaptive_regime_sign_window_settlements);
    }

    if (cfg_.adaptive_regime_enabled &&
        observed_funding_valid &&
        settlement_advanced)
    {
        const bool history_ready =
            funding_settlement_sign_history_.size() >= cfg_.adaptive_regime_min_samples;
        if (history_ready) {
            const auto same_sign_ratio = SameSignPersistenceRatio(funding_settlement_sign_history_);
            if (same_sign_ratio.has_value()) {
                if (*same_sign_ratio >= cfg_.adaptive_regime_sign_persist_high) {
                    // Trending: easier entry, slower exit.
                    adaptive_regime_entry_persistence_cached_ = cfg_.adaptive_regime_entry_persistence_low;
                    adaptive_regime_exit_persistence_cached_ = cfg_.adaptive_regime_exit_persistence_low;
                }
                else if (*same_sign_ratio <= cfg_.adaptive_regime_sign_persist_low) {
                    // Choppy: harder entry, faster exit.
                    adaptive_regime_entry_persistence_cached_ = cfg_.adaptive_regime_entry_persistence_high;
                    adaptive_regime_exit_persistence_cached_ = cfg_.adaptive_regime_exit_persistence_high;
                }
                else {
                    // Neutral regime.
                    adaptive_regime_entry_persistence_cached_ = cfg_.adaptive_regime_entry_persistence_mid;
                    adaptive_regime_exit_persistence_cached_ = cfg_.adaptive_regime_exit_persistence_mid;
                }
                adaptive_regime_ready_ = true;
            }
        }
    }

    if (cfg_.adaptive_gate_enabled || cfg_.adaptive_funding_soft_gate_enabled) {
        const bool funding_history_ready =
            funding_settlement_history_.size() >= cfg_.adaptive_funding_min_samples;
        const bool should_refresh_funding =
            funding_history_ready &&
            (!adaptive_funding_thresholds_ready_ || settlement_advanced);
        if (should_refresh_funding) {
            const auto entry_q = QuantileFromDeque(
                funding_settlement_history_,
                cfg_.adaptive_funding_entry_quantile);
            const auto exit_q = QuantileFromDeque(
                funding_settlement_history_,
                cfg_.adaptive_funding_exit_quantile);
            if (entry_q.has_value()) {
                if (cfg_.adaptive_funding_soft_gate_enabled) {
                    adaptive_funding_entry_min_cached_rate_ = std::clamp(
                        *entry_q,
                        cfg_.adaptive_funding_entry_floor_rate,
                        cfg_.adaptive_funding_entry_cap_rate);
                }
                else {
                    adaptive_funding_entry_min_cached_rate_ =
                        std::max(cfg_.entry_min_funding_rate, *entry_q);
                }
            }
            if (exit_q.has_value()) {
                if (cfg_.adaptive_funding_soft_gate_enabled) {
                    const double soft_exit_from_entry =
                        adaptive_funding_entry_min_cached_rate_ * cfg_.adaptive_funding_exit_ratio;
                    adaptive_funding_exit_min_cached_rate_ = std::min(
                        adaptive_funding_entry_min_cached_rate_,
                        std::max(*exit_q, soft_exit_from_entry));
                }
                else {
                    adaptive_funding_exit_min_cached_rate_ =
                        std::max(cfg_.exit_min_funding_rate, *exit_q);
                }
            }
            adaptive_funding_exit_min_cached_rate_ = std::min(
                adaptive_funding_exit_min_cached_rate_,
                adaptive_funding_entry_min_cached_rate_);
            adaptive_funding_thresholds_ready_ = true;
        }

        if (cfg_.adaptive_gate_enabled && static_basis_gate_configured) {
            const bool basis_history_ready =
                basis_abs_history_.size() >= cfg_.adaptive_basis_min_samples;
            bool should_refresh_basis = false;
            if (basis_history_ready) {
                if (!adaptive_basis_thresholds_ready_) {
                    should_refresh_basis = true;
                }
                else if (cfg_.adaptive_basis_refresh_ms == 0) {
                    should_refresh_basis = true;
                }
                else if (out.ts_ms < adaptive_basis_last_refresh_ts_) {
                    should_refresh_basis = true;
                }
                else if (out.ts_ms >= adaptive_basis_last_refresh_ts_ + cfg_.adaptive_basis_refresh_ms) {
                    should_refresh_basis = true;
                }
            }
            if (should_refresh_basis) {
                const auto entry_q = QuantileFromDeque(
                    basis_abs_history_,
                    cfg_.adaptive_basis_entry_quantile);
                const auto exit_q = QuantileFromDeque(
                    basis_abs_history_,
                    cfg_.adaptive_basis_exit_quantile);
                if (entry_q.has_value()) {
                    const double adaptive_entry = std::max(cfg_.adaptive_basis_floor_pct, *entry_q);
                    adaptive_basis_entry_max_cached_pct_ =
                        std::min(cfg_.entry_max_basis_pct, adaptive_entry);
                }
                if (exit_q.has_value()) {
                    const double adaptive_exit = std::max(cfg_.adaptive_basis_floor_pct, *exit_q);
                    adaptive_basis_exit_max_cached_pct_ =
                        std::min(cfg_.exit_max_basis_pct, adaptive_exit);
                }
                adaptive_basis_exit_max_cached_pct_ = std::max(
                    adaptive_basis_exit_max_cached_pct_,
                    adaptive_basis_entry_max_cached_pct_);
                adaptive_basis_thresholds_ready_ = true;
                adaptive_basis_last_refresh_ts_ = out.ts_ms;
            }
        }

        if (adaptive_funding_thresholds_ready_) {
            entry_min_funding_rate = adaptive_funding_entry_min_cached_rate_;
            exit_min_funding_rate = adaptive_funding_exit_min_cached_rate_;
        }
        if (cfg_.adaptive_gate_enabled && adaptive_basis_thresholds_ready_) {
            entry_max_basis_pct = adaptive_basis_entry_max_cached_pct_;
            exit_max_basis_pct = adaptive_basis_exit_max_cached_pct_;
        }
    }

    if (cfg_.adaptive_confidence_enabled &&
        observed_funding_valid &&
        settlement_advanced &&
        funding_settlement_history_.size() >= cfg_.adaptive_confidence_min_samples)
    {
        const auto low_q = QuantileFromDeque(
            funding_settlement_history_,
            cfg_.adaptive_confidence_low_quantile);
        const auto high_q = QuantileFromDeque(
            funding_settlement_history_,
            cfg_.adaptive_confidence_high_quantile);
        if (low_q.has_value() && high_q.has_value()) {
            const double width = std::max(*high_q - *low_q, kGateEpsilon);
            const double regime_raw = Clamp01((funding_settlement_signal - *low_q) / width);
            const double target_multiplier =
                cfg_.adaptive_confidence_floor +
                (cfg_.adaptive_confidence_ceiling - cfg_.adaptive_confidence_floor) * regime_raw;
            if (!adaptive_confidence_ready_) {
                adaptive_confidence_multiplier_ = target_multiplier;
                adaptive_confidence_ready_ = true;
            }
            else {
                const double alpha = cfg_.adaptive_confidence_ema_alpha;
                adaptive_confidence_multiplier_ =
                    adaptive_confidence_multiplier_ * (1.0 - alpha) +
                    target_multiplier * alpha;
            }
            adaptive_confidence_multiplier_ = std::clamp(
                adaptive_confidence_multiplier_,
                cfg_.adaptive_confidence_floor,
                cfg_.adaptive_confidence_ceiling);
            if (cfg_.adaptive_confidence_bucket_step > 0.0) {
                const double step = cfg_.adaptive_confidence_bucket_step;
                const double raw_bucket =
                    (adaptive_confidence_multiplier_ - cfg_.adaptive_confidence_floor) / step;
                const double rounded_bucket = std::round(raw_bucket);
                adaptive_confidence_multiplier_ =
                    cfg_.adaptive_confidence_floor + rounded_bucket * step;
                adaptive_confidence_multiplier_ = std::clamp(
                    adaptive_confidence_multiplier_,
                    cfg_.adaptive_confidence_floor,
                    cfg_.adaptive_confidence_ceiling);
            }
        }
    }

    if (cfg_.adaptive_structure_enabled &&
        observed_funding_valid &&
        settlement_advanced &&
        funding_settlement_history_.size() >= cfg_.adaptive_structure_min_samples)
    {
        // Funding-structure quality score:
        //   quality = normalized_funding - w1*negative_share - w2*negative_run_norm
        // mapped to [floor, ceiling] with EMA smoothing.
        double normalized_funding =
            Clamp01(0.5 + 0.5 * std::tanh(funding_settlement_signal / FundingConfidenceScale()));
        const auto low_q = QuantileFromDeque(
            funding_settlement_history_,
            cfg_.adaptive_confidence_low_quantile);
        const auto high_q = QuantileFromDeque(
            funding_settlement_history_,
            cfg_.adaptive_confidence_high_quantile);
        if (low_q.has_value() && high_q.has_value()) {
            const double width = std::max(*high_q - *low_q, kGateEpsilon);
            normalized_funding = Clamp01((funding_settlement_signal - *low_q) / width);
        }

        const double negative_share = NegativeShareRatio(funding_settlement_sign_history_).value_or(0.0);
        const double negative_run_norm = Clamp01(
            static_cast<double>(TrailingNegativeRun(funding_settlement_sign_history_)) /
            cfg_.adaptive_structure_neg_run_norm);

        const double raw_quality = normalized_funding
            - cfg_.adaptive_structure_neg_share_weight * negative_share
            - cfg_.adaptive_structure_neg_run_weight * negative_run_norm;
        const double quality = Clamp01(raw_quality);
        const double target_multiplier =
            cfg_.adaptive_structure_floor +
            (cfg_.adaptive_structure_ceiling - cfg_.adaptive_structure_floor) * quality;

        if (!adaptive_structure_ready_) {
            adaptive_structure_multiplier_ = target_multiplier;
            adaptive_structure_ready_ = true;
        }
        else {
            const double alpha = cfg_.adaptive_structure_ema_alpha;
            adaptive_structure_multiplier_ =
                adaptive_structure_multiplier_ * (1.0 - alpha) +
                target_multiplier * alpha;
        }

        adaptive_structure_multiplier_ = std::clamp(
            adaptive_structure_multiplier_,
            cfg_.adaptive_structure_floor,
            cfg_.adaptive_structure_ceiling);

        if (cfg_.adaptive_structure_bucket_step > 0.0) {
            const double step = cfg_.adaptive_structure_bucket_step;
            const double raw_bucket =
                (adaptive_structure_multiplier_ - cfg_.adaptive_structure_floor) / step;
            const double rounded_bucket = std::round(raw_bucket);
            adaptive_structure_multiplier_ =
                cfg_.adaptive_structure_floor + rounded_bucket * step;
            adaptive_structure_multiplier_ = std::clamp(
                adaptive_structure_multiplier_,
                cfg_.adaptive_structure_floor,
                cfg_.adaptive_structure_ceiling);
        }
    }

    const bool enable_funding_gate =
        (std::abs(entry_min_funding_rate) > kGateEpsilon) ||
        (std::abs(exit_min_funding_rate) > kGateEpsilon);
    const bool enable_basis_gate =
        (entry_max_basis_pct < 1.0 - kGateEpsilon) ||
        (exit_max_basis_pct < 1.0 - kGateEpsilon);

    if (enable_funding_gate) {
        const bool use_nowcast_for_entry_gate =
            cfg_.funding_nowcast_enabled &&
            (cfg_.funding_nowcast_use_for_gates || cfg_.funding_nowcast_use_for_entry_gate);
        const bool use_nowcast_for_exit_gate =
            cfg_.funding_nowcast_enabled &&
            (cfg_.funding_nowcast_use_for_gates || cfg_.funding_nowcast_use_for_exit_gate);
        const bool can_evaluate_with_observed_settlement =
            observed_funding_valid &&
            cfg_.lock_funding_to_settlement;

        const bool evaluate_entry_with_observed =
            can_evaluate_with_observed_settlement && !use_nowcast_for_entry_gate;
        if (evaluate_entry_with_observed) {
            if (settlement_advanced) {
                if (funding_settlement_signal >= entry_min_funding_rate) {
                    funding_entry_good_streak_ += 1;
                }
                else {
                    funding_entry_good_streak_ = 0;
                }
            }
        }
        else {
            bool sample_entry_now = true;
            if (use_nowcast_for_entry_gate && cfg_.funding_nowcast_gate_sample_ms > 0) {
                sample_entry_now =
                    (!has_last_nowcast_entry_gate_eval_ts_) ||
                    (out.ts_ms >= last_nowcast_entry_gate_eval_ts_ + cfg_.funding_nowcast_gate_sample_ms);
                if (sample_entry_now) {
                    has_last_nowcast_entry_gate_eval_ts_ = true;
                    last_nowcast_entry_gate_eval_ts_ = out.ts_ms;
                }
            }
            if (sample_entry_now) {
                const double entry_gate_funding_value =
                    use_nowcast_for_entry_gate ? funding_nowcast_signal : funding_proxy_ema_;
                if (entry_gate_funding_value >= entry_min_funding_rate) {
                    funding_entry_good_streak_ += 1;
                }
                else {
                    funding_entry_good_streak_ = 0;
                }
            }
        }

        const bool evaluate_exit_with_observed =
            can_evaluate_with_observed_settlement && !use_nowcast_for_exit_gate;
        if (evaluate_exit_with_observed) {
            if (settlement_advanced) {
                if (funding_settlement_signal < exit_min_funding_rate) {
                    funding_exit_bad_streak_ += 1;
                }
                else {
                    funding_exit_bad_streak_ = 0;
                }
            }
        }
        else {
            bool sample_exit_now = true;
            if (use_nowcast_for_exit_gate && cfg_.funding_nowcast_gate_sample_ms > 0) {
                sample_exit_now =
                    (!has_last_nowcast_exit_gate_eval_ts_) ||
                    (out.ts_ms >= last_nowcast_exit_gate_eval_ts_ + cfg_.funding_nowcast_gate_sample_ms);
                if (sample_exit_now) {
                    has_last_nowcast_exit_gate_eval_ts_ = true;
                    last_nowcast_exit_gate_eval_ts_ = out.ts_ms;
                }
            }
            if (sample_exit_now) {
                const double exit_gate_funding_value =
                    use_nowcast_for_exit_gate ? funding_nowcast_signal : funding_proxy_ema_;
                if (exit_gate_funding_value < exit_min_funding_rate) {
                    funding_exit_bad_streak_ += 1;
                }
                else {
                    funding_exit_bad_streak_ = 0;
                }
            }
        }
    }
    else {
        funding_entry_good_streak_ = 0;
        funding_exit_bad_streak_ = 0;
    }

    if (cfg_.hard_negative_funding_rate > -1.0) {
        const bool evaluate_hard_negative_with_observed =
            observed_funding_valid &&
            cfg_.lock_funding_to_settlement &&
            !cfg_.funding_nowcast_use_for_gates;
        if (evaluate_hard_negative_with_observed) {
            if (settlement_advanced) {
                if (funding_settlement_signal <= cfg_.hard_negative_funding_rate) {
                    funding_hard_negative_streak_ += 1;
                }
                else {
                    funding_hard_negative_streak_ = 0;
                }
            }
        }
        else {
            const double hard_negative_funding_value =
                cfg_.funding_nowcast_use_for_gates ? funding_nowcast_signal : funding_proxy_ema_;
            if (hard_negative_funding_value <= cfg_.hard_negative_funding_rate) {
                funding_hard_negative_streak_ += 1;
            }
            else {
                funding_hard_negative_streak_ = 0;
            }
        }
    }
    else {
        funding_hard_negative_streak_ = 0;
    }

    bool pre_settlement_negative_exit = false;
    uint64_t pre_settlement_reentry_block_until_ts = 0;
    if (cfg_.pre_settlement_negative_exit_enabled &&
        cfg_.pre_settlement_negative_exit_threshold > -1.0 &&
        (!cfg_.pre_settlement_negative_exit_require_funding_gate || enable_funding_gate) &&
        observed_funding_valid &&
        funding_settlement_time.has_value() &&
        cfg_.funding_nowcast_interval_ms > 0)
    {
        const uint64_t next_settlement_time =
            *funding_settlement_time + cfg_.funding_nowcast_interval_ms;
        if (out.ts_ms <= next_settlement_time) {
            const uint64_t remaining_ms = next_settlement_time - out.ts_ms;
            if (remaining_ms <= cfg_.pre_settlement_negative_exit_lookahead_ms) {
                // With linear nowcast, the projected settlement endpoint is current basis-proxy funding.
                const double projected_next_funding = funding_proxy;
                pre_settlement_negative_exit =
                    projected_next_funding <= cfg_.pre_settlement_negative_exit_threshold;
                if (pre_settlement_negative_exit) {
                    pre_settlement_reentry_block_until_ts =
                        next_settlement_time + cfg_.pre_settlement_negative_exit_reentry_buffer_ms;
                }
            }
        }
    }

    bool entered_by_watchdog = false;
    if (active_) {
        const bool can_exit = (cfg_.min_hold_ms == 0) ||
            (out.ts_ms >= active_since_ts_ + cfg_.min_hold_ms);
        uint32_t exit_persistence = cfg_.exit_persistence_settlements;
        if (cfg_.adaptive_regime_enabled && adaptive_regime_ready_) {
            exit_persistence = adaptive_regime_exit_persistence_cached_;
        }
        const bool exit_funding = enable_funding_gate &&
            (funding_exit_bad_streak_ >= exit_persistence);
        const bool hard_negative_funding =
            (cfg_.hard_negative_funding_rate > -1.0) &&
            (funding_hard_negative_streak_ >= cfg_.hard_negative_persistence_settlements);
        const bool exit_basis =
            enable_basis_gate && (std::abs(basis_pct) > exit_max_basis_pct);
        const bool catastrophic_basis =
            enable_basis_gate && (std::abs(basis_pct) > (exit_max_basis_pct * 2.0));
        const bool normal_exit = can_exit && (exit_funding || exit_basis);
        if (normal_exit || catastrophic_basis || hard_negative_funding || pre_settlement_negative_exit ||
            mark_index_hard_breached) {
            active_ = false;
            last_exit_ts_ = out.ts_ms;
            if (pre_settlement_negative_exit && pre_settlement_reentry_block_until_ts > out.ts_ms) {
                pre_settlement_reentry_block_until_ts_ = pre_settlement_reentry_block_until_ts;
            }
            active_since_ts_ = 0;
        }
    }
    else {
        const bool cooldown_ok = (last_exit_ts_ == 0) ||
            (cfg_.cooldown_ms == 0) ||
            (out.ts_ms >= last_exit_ts_ + cfg_.cooldown_ms);
        const bool pre_settlement_reentry_ok =
            (pre_settlement_reentry_block_until_ts_ == 0) ||
            (out.ts_ms >= pre_settlement_reentry_block_until_ts_);
        uint32_t entry_persistence = cfg_.entry_persistence_settlements;
        if (cfg_.adaptive_regime_enabled && adaptive_regime_ready_) {
            entry_persistence = adaptive_regime_entry_persistence_cached_;
        }
        const bool enter_funding = !enable_funding_gate ||
            (funding_entry_good_streak_ >= entry_persistence);
        const bool enter_basis =
            !enable_basis_gate || (std::abs(basis_pct) <= entry_max_basis_pct);
        const bool watchdog_allows_entry =
            (cfg_.inactivity_watchdog_settlements > 0) &&
            (inactive_settlement_streak_ >= cfg_.inactivity_watchdog_settlements) &&
            (funding_settlement_signal >= cfg_.inactivity_watchdog_min_rate);
        if (!mark_index_hard_breached &&
            cooldown_ok &&
            pre_settlement_reentry_ok &&
            enter_basis &&
            (enter_funding || watchdog_allows_entry)) {
            active_ = true;
            active_since_ts_ = out.ts_ms;
            entered_by_watchdog = watchdog_allows_entry && !enter_funding;
        }
    }

    if (observed_funding_valid && settlement_advanced) {
        if (active_) {
            inactive_settlement_streak_ = 0;
        }
        else {
            inactive_settlement_streak_ += 1;
        }
    }

    // Active by design: funding is earned over time, so urgency is low.
    out.status = active_ ? SignalStatus::Active : SignalStatus::Inactive;
    if (active_) {
        Config score_cfg = cfg_;
        score_cfg.entry_min_funding_rate = entry_min_funding_rate;
        score_cfg.exit_min_funding_rate = exit_min_funding_rate;
        score_cfg.entry_max_basis_pct = entry_max_basis_pct;
        score_cfg.exit_max_basis_pct = exit_max_basis_pct;
        const double funding_score =
            NormalizedFundingScore(score_cfg, funding_proxy_ema_, enable_funding_gate);
        const double basis_score =
            NormalizedBasisScore(score_cfg, basis_pct, enable_basis_gate);
        out.confidence = Clamp01(funding_score * basis_score);
        if (cfg_.adaptive_confidence_enabled && adaptive_confidence_ready_) {
            out.confidence = Clamp01(out.confidence * adaptive_confidence_multiplier_);
        }
        if (cfg_.adaptive_structure_enabled && adaptive_structure_ready_) {
            out.confidence = Clamp01(out.confidence * adaptive_structure_multiplier_);
        }
        if (mark_index_bps.has_value() &&
            cfg_.mark_index_soft_derisk_start_bps > 0.0 &&
            cfg_.mark_index_soft_derisk_full_bps >= cfg_.mark_index_soft_derisk_start_bps)
        {
            const double abs_bps = std::abs(*mark_index_bps);
            double derisk_scale = 1.0;
            if (abs_bps > cfg_.mark_index_soft_derisk_start_bps) {
                if (cfg_.mark_index_soft_derisk_full_bps <= cfg_.mark_index_soft_derisk_start_bps + kGateEpsilon) {
                    derisk_scale = cfg_.mark_index_soft_derisk_min_confidence_scale;
                }
                else {
                    const double progress = Clamp01(
                        (abs_bps - cfg_.mark_index_soft_derisk_start_bps) /
                        (cfg_.mark_index_soft_derisk_full_bps - cfg_.mark_index_soft_derisk_start_bps));
                    derisk_scale =
                        1.0 - progress * (1.0 - cfg_.mark_index_soft_derisk_min_confidence_scale);
                }
            }
            out.confidence = Clamp01(out.confidence * derisk_scale);
        }
        if (entered_by_watchdog) {
            out.confidence = std::max(out.confidence, cfg_.inactivity_watchdog_min_confidence);
        }
    }
    else {
        out.confidence = 0.0;
    }
    out.urgency = SignalUrgency::Low;
    return out;
}

} // namespace QTrading::Signal
