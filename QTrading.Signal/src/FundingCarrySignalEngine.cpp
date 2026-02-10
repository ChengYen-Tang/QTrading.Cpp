#include "Signal/FundingCarrySignalEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>

namespace QTrading::Signal {
namespace {
constexpr double kBasisToFundingScale = 0.25;
constexpr double kFundingProxyEmaAlpha = 0.10;
constexpr double kGateEpsilon = 1e-12;
constexpr double kDefaultFundingConfidenceScale = 0.00005;

void OverrideDoubleFromEnv(const char* name, double& value)
{
    const char* raw = std::getenv(name);
    if (!raw || !*raw) {
        return;
    }
    try {
        value = std::stod(raw);
    }
    catch (...) {
        // Ignore malformed env values and keep code-level defaults.
    }
}

void OverrideUint64FromEnv(const char* name, uint64_t& value)
{
    const char* raw = std::getenv(name);
    if (!raw || !*raw) {
        return;
    }
    try {
        value = static_cast<uint64_t>(std::stoull(raw));
    }
    catch (...) {
        // Ignore malformed env values and keep code-level defaults.
    }
}

void OverrideUint32FromEnv(const char* name, uint32_t& value)
{
    const char* raw = std::getenv(name);
    if (!raw || !*raw) {
        return;
    }
    try {
        value = static_cast<uint32_t>(std::stoul(raw));
    }
    catch (...) {
        // Ignore malformed env values and keep code-level defaults.
    }
}

void OverrideBoolFromEnv(const char* name, bool& value)
{
    const char* raw = std::getenv(name);
    if (!raw || !*raw) {
        return;
    }
    const std::string token(raw);
    if (token == "1" || token == "true" || token == "TRUE" || token == "True") {
        value = true;
        return;
    }
    if (token == "0" || token == "false" || token == "FALSE" || token == "False") {
        value = false;
        return;
    }
    try {
        value = (std::stoll(token) != 0);
    }
    catch (...) {
        // Ignore malformed env values and keep code-level defaults.
    }
}

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

double FundingConfidenceScale()
{
    static const double value = [] {
        double scale = kDefaultFundingConfidenceScale;
        const char* raw = std::getenv("QTR_FC_CONFIDENCE_FUNDING_SCALE");
        if (raw && *raw) {
            try {
                scale = std::abs(std::stod(raw));
            }
            catch (...) {
                // Ignore malformed env values and keep default.
            }
        }
        return std::max(scale, kGateEpsilon);
    }();
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
}

FundingCarrySignalEngine::FundingCarrySignalEngine(Config cfg)
    : cfg_(std::move(cfg))
{
    // Optional env overrides so research sweeps can tune behavior without editing service wiring.
    OverrideDoubleFromEnv("QTR_FC_ENTRY_MIN_FUNDING_RATE", cfg_.entry_min_funding_rate);
    OverrideDoubleFromEnv("QTR_FC_EXIT_MIN_FUNDING_RATE", cfg_.exit_min_funding_rate);
    OverrideDoubleFromEnv("QTR_FC_HARD_NEGATIVE_FUNDING_RATE", cfg_.hard_negative_funding_rate);
    OverrideDoubleFromEnv("QTR_FC_ENTRY_MAX_BASIS_PCT", cfg_.entry_max_basis_pct);
    OverrideDoubleFromEnv("QTR_FC_EXIT_MAX_BASIS_PCT", cfg_.exit_max_basis_pct);
    OverrideUint64FromEnv("QTR_FC_COOLDOWN_MS", cfg_.cooldown_ms);
    OverrideUint64FromEnv("QTR_FC_MIN_HOLD_MS", cfg_.min_hold_ms);
    OverrideUint32FromEnv("QTR_FC_ENTRY_PERSISTENCE_SETTLEMENTS", cfg_.entry_persistence_settlements);
    OverrideUint32FromEnv("QTR_FC_EXIT_PERSISTENCE_SETTLEMENTS", cfg_.exit_persistence_settlements);
    OverrideBoolFromEnv("QTR_FC_LOCK_FUNDING_TO_SETTLEMENT", cfg_.lock_funding_to_settlement);
    OverrideUint64FromEnv("QTR_FC_OBSERVED_FUNDING_MAX_AGE_MS", cfg_.observed_funding_max_age_ms);
    cfg_.entry_persistence_settlements = std::max(cfg_.entry_persistence_settlements, 1u);
    cfg_.exit_persistence_settlements = std::max(cfg_.exit_persistence_settlements, 1u);
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
        spot_id_ < market->klines_by_id.size() &&
        perp_id_ < market->klines_by_id.size())
    {
        return market->klines_by_id[spot_id_].has_value() &&
            market->klines_by_id[perp_id_].has_value();
    }

    const auto& klines = market->klines;
    auto it_spot = klines.find(cfg_.spot_symbol);
    auto it_perp = klines.find(cfg_.perp_symbol);
    if (it_spot == klines.end() || it_perp == klines.end()) {
        return false;
    }
    return it_spot->second.has_value() && it_perp->second.has_value();
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
        spot_id_ < market->klines_by_id.size() &&
        perp_id_ < market->klines_by_id.size())
    {
        const auto& spot_opt = market->klines_by_id[spot_id_];
        const auto& perp_opt = market->klines_by_id[perp_id_];
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
    double funding_signal = funding_proxy;
    if (latest_observed_funding_rate.has_value() && latest_observed_funding_time.has_value()) {
        const auto funding_time = *latest_observed_funding_time;
        const bool not_future = funding_time <= out.ts_ms;
        const bool within_age = not_future && (
            !cfg_.observed_funding_max_age_ms ||
            ((out.ts_ms - funding_time) <= cfg_.observed_funding_max_age_ms));
        observed_funding_valid = not_future && within_age;
        if (observed_funding_valid) {
            funding_signal = *latest_observed_funding_rate;
            settlement_advanced =
                (!has_last_observed_funding_time_) ||
                (funding_time != last_observed_funding_time_);
            if (settlement_advanced) {
                has_last_observed_funding_time_ = true;
                last_observed_funding_time_ = funding_time;
            }
        }
    }
    const bool enable_funding_gate =
        (std::abs(cfg_.entry_min_funding_rate) > kGateEpsilon) ||
        (std::abs(cfg_.exit_min_funding_rate) > kGateEpsilon);
    const bool enable_basis_gate =
        (cfg_.entry_max_basis_pct < 1.0 - kGateEpsilon) ||
        (cfg_.exit_max_basis_pct < 1.0 - kGateEpsilon);

    const bool update_funding_ema = !cfg_.lock_funding_to_settlement ||
        !observed_funding_valid ||
        settlement_advanced;
    if (!funding_proxy_initialized_) {
        funding_proxy_ema_ = funding_signal;
        funding_proxy_initialized_ = true;
    }
    else if (update_funding_ema) {
        funding_proxy_ema_ =
            funding_proxy_ema_ * (1.0 - kFundingProxyEmaAlpha) +
            funding_signal * kFundingProxyEmaAlpha;
    }

    if (enable_funding_gate) {
        const bool evaluate_streaks_with_observed =
            observed_funding_valid && cfg_.lock_funding_to_settlement;
        if (evaluate_streaks_with_observed) {
            if (settlement_advanced) {
                if (funding_signal >= cfg_.entry_min_funding_rate) {
                    funding_entry_good_streak_ += 1;
                }
                else {
                    funding_entry_good_streak_ = 0;
                }
                if (funding_signal < cfg_.exit_min_funding_rate) {
                    funding_exit_bad_streak_ += 1;
                }
                else {
                    funding_exit_bad_streak_ = 0;
                }
            }
        }
        else {
            funding_entry_good_streak_ =
                (funding_proxy_ema_ >= cfg_.entry_min_funding_rate) ? 1u : 0u;
            funding_exit_bad_streak_ =
                (funding_proxy_ema_ < cfg_.exit_min_funding_rate) ? 1u : 0u;
        }
    }
    else {
        funding_entry_good_streak_ = 0;
        funding_exit_bad_streak_ = 0;
    }

    if (active_) {
        const bool can_exit = (cfg_.min_hold_ms == 0) ||
            (out.ts_ms >= active_since_ts_ + cfg_.min_hold_ms);
        const bool exit_funding = enable_funding_gate &&
            (funding_exit_bad_streak_ >= cfg_.exit_persistence_settlements);
        const bool hard_negative_funding =
            (cfg_.hard_negative_funding_rate > -1.0) &&
            (funding_signal <= cfg_.hard_negative_funding_rate);
        const bool exit_basis =
            enable_basis_gate && (std::abs(basis_pct) > cfg_.exit_max_basis_pct);
        const bool catastrophic_basis =
            enable_basis_gate && (std::abs(basis_pct) > (cfg_.exit_max_basis_pct * 2.0));
        const bool normal_exit = can_exit && (exit_funding || exit_basis);
        if (normal_exit || catastrophic_basis || hard_negative_funding) {
            active_ = false;
            last_exit_ts_ = out.ts_ms;
            active_since_ts_ = 0;
        }
    }
    else {
        const bool cooldown_ok = (last_exit_ts_ == 0) ||
            (cfg_.cooldown_ms == 0) ||
            (out.ts_ms >= last_exit_ts_ + cfg_.cooldown_ms);
        const bool enter_funding = !enable_funding_gate ||
            (funding_entry_good_streak_ >= cfg_.entry_persistence_settlements);
        const bool enter_basis =
            !enable_basis_gate || (std::abs(basis_pct) <= cfg_.entry_max_basis_pct);
        if (cooldown_ok && enter_funding && enter_basis) {
            active_ = true;
            active_since_ts_ = out.ts_ms;
        }
    }

    // Active by design: funding is earned over time, so urgency is low.
    out.status = active_ ? SignalStatus::Active : SignalStatus::Inactive;
    if (active_) {
        const double funding_score =
            NormalizedFundingScore(cfg_, funding_proxy_ema_, enable_funding_gate);
        const double basis_score =
            NormalizedBasisScore(cfg_, basis_pct, enable_basis_gate);
        out.confidence = Clamp01(funding_score * basis_score);
    }
    else {
        out.confidence = 0.0;
    }
    out.urgency = SignalUrgency::Low;
    return out;
}

} // namespace QTrading::Signal
