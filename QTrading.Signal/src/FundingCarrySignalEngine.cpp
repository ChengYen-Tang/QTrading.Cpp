#include "Signal/FundingCarrySignalEngine.hpp"

#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>

namespace QTrading::Signal {
namespace {
constexpr double kBasisToFundingScale = 0.25;
constexpr double kFundingProxyEmaAlpha = 0.10;
constexpr double kGateEpsilon = 1e-12;

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
}

FundingCarrySignalEngine::FundingCarrySignalEngine(Config cfg)
    : cfg_(std::move(cfg))
{
    // Optional env overrides so research sweeps can tune behavior without editing service wiring.
    OverrideDoubleFromEnv("QTR_FC_ENTRY_MIN_FUNDING_RATE", cfg_.entry_min_funding_rate);
    OverrideDoubleFromEnv("QTR_FC_EXIT_MIN_FUNDING_RATE", cfg_.exit_min_funding_rate);
    OverrideDoubleFromEnv("QTR_FC_ENTRY_MAX_BASIS_PCT", cfg_.entry_max_basis_pct);
    OverrideDoubleFromEnv("QTR_FC_EXIT_MAX_BASIS_PCT", cfg_.exit_max_basis_pct);
    OverrideUint64FromEnv("QTR_FC_COOLDOWN_MS", cfg_.cooldown_ms);
    OverrideUint64FromEnv("QTR_FC_MIN_HOLD_MS", cfg_.min_hold_ms);
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
    const bool enable_funding_gate =
        (std::abs(cfg_.entry_min_funding_rate) > kGateEpsilon) ||
        (std::abs(cfg_.exit_min_funding_rate) > kGateEpsilon);
    const bool enable_basis_gate =
        (cfg_.entry_max_basis_pct < 1.0 - kGateEpsilon) ||
        (cfg_.exit_max_basis_pct < 1.0 - kGateEpsilon);

    if (!funding_proxy_initialized_) {
        funding_proxy_ema_ = funding_proxy;
        funding_proxy_initialized_ = true;
    }
    else {
        funding_proxy_ema_ =
            funding_proxy_ema_ * (1.0 - kFundingProxyEmaAlpha) +
            funding_proxy * kFundingProxyEmaAlpha;
    }
    if (active_) {
        const bool can_exit = (cfg_.min_hold_ms == 0) ||
            (out.ts_ms >= active_since_ts_ + cfg_.min_hold_ms);
        const bool exit_funding =
            enable_funding_gate && (funding_proxy_ema_ < cfg_.exit_min_funding_rate);
        const bool exit_basis =
            enable_basis_gate && (std::abs(basis_pct) > cfg_.exit_max_basis_pct);
        const bool catastrophic_basis =
            enable_basis_gate && (std::abs(basis_pct) > (cfg_.exit_max_basis_pct * 2.0));
        const bool normal_exit = can_exit && (exit_funding || exit_basis);
        if (normal_exit || catastrophic_basis) {
            active_ = false;
            last_exit_ts_ = out.ts_ms;
            active_since_ts_ = 0;
        }
    }
    else {
        const bool cooldown_ok = (last_exit_ts_ == 0) ||
            (cfg_.cooldown_ms == 0) ||
            (out.ts_ms >= last_exit_ts_ + cfg_.cooldown_ms);
        const bool enter_funding =
            !enable_funding_gate || (funding_proxy_ema_ >= cfg_.entry_min_funding_rate);
        const bool enter_basis =
            !enable_basis_gate || (std::abs(basis_pct) <= cfg_.entry_max_basis_pct);
        if (cooldown_ok && enter_funding && enter_basis) {
            active_ = true;
            active_since_ts_ = out.ts_ms;
        }
    }

    // Active by design: funding is earned over time, so urgency is low.
    out.status = active_ ? SignalStatus::Active : SignalStatus::Inactive;
    out.confidence = active_ ? 1.0 : 0.0;
    out.urgency = SignalUrgency::Low;
    return out;
}

} // namespace QTrading::Signal
