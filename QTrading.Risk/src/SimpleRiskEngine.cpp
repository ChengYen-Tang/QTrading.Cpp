#include "Risk/SimpleRiskEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_map>

namespace {

std::optional<double> GetPriceBySymbol(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market,
    const std::string& symbol)
{
    if (!market) {
        return std::nullopt;
    }

    if (market->symbols && !market->klines_by_id.empty()) {
        const auto& symbols = *market->symbols;
        for (std::size_t i = 0; i < symbols.size(); ++i) {
            if (symbols[i] == symbol && i < market->klines_by_id.size()) {
                const auto& opt = market->klines_by_id[i];
                if (opt.has_value()) {
                    return opt->ClosePrice;
                }
                return std::nullopt;
            }
        }
    }

    const auto& klines = market->klines;
    auto it = klines.find(symbol);
    if (it == klines.end() || !it->second.has_value()) {
        return std::nullopt;
    }
    return it->second->ClosePrice;
}

double SignedNotionalFromPosition(const QTrading::dto::Position& pos, double price)
{
    const double sign = pos.is_long ? 1.0 : -1.0;
    return pos.quantity * price * sign;
}

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

double Clamp01(double value)
{
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

double ClampAlpha(double value)
{
    return Clamp01(value);
}

double ClampNonNegative(double value)
{
    if (!std::isfinite(value) || value < 0.0) {
        return 0.0;
    }
    return value;
}

double EffectiveLegNotional(const QTrading::Risk::SimpleRiskEngine::Config& cfg)
{
    const double desired = std::abs(cfg.notional_usdt);
    const double cap = std::isfinite(cfg.max_leg_notional_usdt) && (cfg.max_leg_notional_usdt > 0.0)
        ? cfg.max_leg_notional_usdt
        : desired;
    return std::min(desired, cap);
}

} // namespace

namespace QTrading::Risk {

SimpleRiskEngine::SimpleRiskEngine(Config cfg)
    : cfg_(cfg)
{
    // Optional env overrides for research sweeps.
    OverrideDoubleFromEnv("QTR_FC_NOTIONAL_USDT", cfg_.notional_usdt);
    OverrideDoubleFromEnv("QTR_FC_MAX_LEG_NOTIONAL_USDT", cfg_.max_leg_notional_usdt);
    OverrideDoubleFromEnv("QTR_FC_LEVERAGE", cfg_.leverage);
    OverrideDoubleFromEnv("QTR_FC_MAX_LEVERAGE", cfg_.max_leverage);
    OverrideDoubleFromEnv("QTR_FC_REBALANCE_THRESHOLD_RATIO", cfg_.rebalance_threshold_ratio);
    OverrideDoubleFromEnv("QTR_FC_BASIS_SOFT_CAP_PCT", cfg_.basis_soft_cap_pct);
    OverrideDoubleFromEnv("QTR_FC_MIN_NOTIONAL_SCALE", cfg_.min_notional_scale);
    cfg_.min_notional_scale = Clamp01(cfg_.min_notional_scale);
    OverrideDoubleFromEnv("QTR_FC_BASIS_TREND_MAX_ABS", cfg_.basis_trend_max_abs);
    OverrideDoubleFromEnv("QTR_FC_BASIS_TREND_MIN_SCALE", cfg_.basis_trend_min_scale);
    OverrideDoubleFromEnv("QTR_FC_BASIS_TREND_EMA_ALPHA", cfg_.basis_trend_ema_alpha);
    cfg_.basis_trend_min_scale = Clamp01(cfg_.basis_trend_min_scale);
    cfg_.basis_trend_ema_alpha = ClampAlpha(cfg_.basis_trend_ema_alpha);
    OverrideDoubleFromEnv("QTR_FC_NEG_BASIS_THRESHOLD", cfg_.neg_basis_threshold);
    OverrideDoubleFromEnv("QTR_FC_NEG_BASIS_SCALE", cfg_.neg_basis_scale);
    OverrideDoubleFromEnv("QTR_FC_NEG_BASIS_HYSTERESIS_PCT", cfg_.neg_basis_hysteresis_pct);
    cfg_.neg_basis_scale = Clamp01(cfg_.neg_basis_scale);
    cfg_.neg_basis_hysteresis_pct = ClampNonNegative(cfg_.neg_basis_hysteresis_pct);
    OverrideDoubleFromEnv("QTR_FC_BASIS_OVERLAY_CAP", cfg_.basis_overlay_cap);
    OverrideDoubleFromEnv("QTR_FC_BASIS_OVERLAY_STRENGTH", cfg_.basis_overlay_strength);
    OverrideDoubleFromEnv("QTR_FC_BASIS_LEVEL_EMA_ALPHA", cfg_.basis_level_ema_alpha);
    cfg_.basis_overlay_strength = Clamp01(cfg_.basis_overlay_strength);
    cfg_.basis_level_ema_alpha = ClampAlpha(cfg_.basis_level_ema_alpha);
    OverrideDoubleFromEnv("QTR_FC_GROSS_DEVIATION_TRIGGER_RATIO", cfg_.gross_deviation_trigger_ratio);
    OverrideDoubleFromEnv("QTR_FC_GROSS_DEVIATION_TRIGGER_NOTIONAL_THRESHOLD", cfg_.gross_deviation_trigger_notional_threshold);

    for (const auto& kv : cfg_.instrument_types) {
        instrument_registry_.Set(kv.first, kv.second);
    }
}

bool SimpleRiskEngine::is_spot_instrument_(const std::string& instrument) const
{
    return instrument_registry_.Resolve(instrument).type == QTrading::Dto::Trading::InstrumentType::Spot;
}

bool SimpleRiskEngine::is_perp_instrument_(const std::string& instrument) const
{
    return instrument_registry_.Resolve(instrument).type == QTrading::Dto::Trading::InstrumentType::Perp;
}

double SimpleRiskEngine::leverage_for_instrument_(const std::string& instrument) const
{
    const auto& spec = instrument_registry_.Resolve(instrument);
    if (spec.max_leverage <= 1.0) {
        return 1.0;
    }
    const double target = std::max(1.0, cfg_.leverage);
    return std::min(target, spec.max_leverage);
}

RiskTarget SimpleRiskEngine::position(const QTrading::Intent::TradeIntent& intent,
    const AccountState& account,
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    RiskTarget out;
    out.ts_ms = intent.ts_ms;
    out.strategy = intent.strategy;
    out.max_leverage = cfg_.max_leverage;
    out.risk_budget_used = intent.legs.empty() ? 0.0 : 1.0;

    if (intent.legs.empty()) {
        for (const auto& pos : account.positions) {
            out.target_positions[pos.symbol] = 0.0;
            out.leverage[pos.symbol] = leverage_for_instrument_(pos.symbol);
        }
        return out;
    }

    const bool is_carry = (intent.strategy == "funding_carry") ||
        (intent.structure == "delta_neutral_carry");
    const double leg_notional_usdt = EffectiveLegNotional(cfg_);

    if (is_carry && market && intent.legs.size() >= 2) {
        double gross = 0.0;
        double net = 0.0;
        bool perp_is_short = false;
        bool perp_found = false;
        std::optional<double> spot_price_snapshot;
        std::optional<double> perp_price_snapshot;
        std::unordered_map<std::string, double> current_notional;
        current_notional.reserve(intent.legs.size());
        std::unordered_map<std::string, double> price_by_symbol;
        price_by_symbol.reserve(intent.legs.size());

        for (const auto& leg : intent.legs) {
            if (is_perp_instrument_(leg.instrument)) {
                perp_found = true;
                perp_is_short = (leg.side == QTrading::Intent::TradeSide::Short);
            }
            auto price = GetPriceBySymbol(market, leg.instrument);
            if (!price.has_value() || *price <= 0.0) {
                current_notional.clear();
                break;
            }
            price_by_symbol[leg.instrument] = *price;
            if (is_spot_instrument_(leg.instrument)) {
                spot_price_snapshot = *price;
            }
            if (is_perp_instrument_(leg.instrument)) {
                perp_price_snapshot = *price;
            }

            double notional = 0.0;
            for (const auto& pos : account.positions) {
                if (pos.symbol == leg.instrument) {
                    notional += SignedNotionalFromPosition(pos, *price);
                }
            }

            current_notional[leg.instrument] = notional;
            gross += std::abs(notional);
            net += notional;
        }

        bool overlay_active = false;
        if (spot_price_snapshot.has_value() && perp_price_snapshot.has_value()) {
            const double basis_pct = (*perp_price_snapshot - *spot_price_snapshot) / *spot_price_snapshot;
            if (!basis_level_ema_initialized_) {
                basis_level_ema_ = basis_pct;
                basis_level_ema_prev_ = basis_pct;
                basis_level_ema_initialized_ = true;
            }
            else {
                basis_level_ema_prev_ = basis_level_ema_;
                basis_level_ema_ = basis_level_ema_ * (1.0 - cfg_.basis_level_ema_alpha) +
                    basis_pct * cfg_.basis_level_ema_alpha;
            }

            if (cfg_.basis_overlay_cap > 0.0 && cfg_.basis_overlay_strength > 0.0) {
                const double deviation = basis_pct - basis_level_ema_;
                overlay_active = (std::abs(deviation) >= (cfg_.basis_overlay_cap * 0.1));
            }
        }

        bool gross_deviation_exceeded = false;
        if (gross > 0.0 &&
            (leg_notional_usdt >= cfg_.gross_deviation_trigger_notional_threshold) &&
            std::isfinite(cfg_.gross_deviation_trigger_ratio) &&
            cfg_.gross_deviation_trigger_ratio >= 0.0)
        {
            const double target_gross = leg_notional_usdt * static_cast<double>(intent.legs.size());
            if (target_gross > 0.0) {
                const double deviation = std::abs(gross - target_gross) / target_gross;
                gross_deviation_exceeded = (deviation >= cfg_.gross_deviation_trigger_ratio);
            }
        }

        if (!current_notional.empty() && gross > 0.0 && !overlay_active && !gross_deviation_exceeded) {
            const double ratio = std::abs(net) / gross;
            if (ratio < cfg_.rebalance_threshold_ratio) {
                for (const auto& leg : intent.legs) {
                    out.target_positions[leg.instrument] = current_notional[leg.instrument];
                    out.leverage[leg.instrument] = leverage_for_instrument_(leg.instrument);
                }
                return out;
            }
        }

        if (!price_by_symbol.empty()) {
            std::optional<double> spot_price = spot_price_snapshot;
            std::optional<double> perp_price = perp_price_snapshot;
            if (!spot_price.has_value()) {
                spot_price = price_by_symbol.begin()->second;
            }
            if (spot_price.has_value() && *spot_price > 0.0) {
                double scale = 1.0;
                if (perp_price.has_value()) {
                    const double basis_pct = (*perp_price - *spot_price) / *spot_price;

                    if (cfg_.basis_soft_cap_pct > 0.0 && cfg_.basis_soft_cap_pct < 1.0) {
                        const double basis_abs = std::abs(basis_pct);
                        const double raw_scale = 1.0 - (basis_abs / cfg_.basis_soft_cap_pct);
                        double level_scale = Clamp01(raw_scale);
                        if (cfg_.min_notional_scale < 1.0) {
                            level_scale = std::max(cfg_.min_notional_scale, level_scale);
                        }
                        scale = std::min(scale, level_scale);
                    }

                    if (cfg_.neg_basis_scale < 1.0) {
                        if (cfg_.neg_basis_hysteresis_pct > 0.0) {
                            const double entry_threshold =
                                cfg_.neg_basis_threshold - cfg_.neg_basis_hysteresis_pct;
                            const double exit_threshold =
                                cfg_.neg_basis_threshold + cfg_.neg_basis_hysteresis_pct;
                            if (!neg_basis_scale_active_ && basis_pct < entry_threshold) {
                                neg_basis_scale_active_ = true;
                            }
                            else if (neg_basis_scale_active_ && basis_pct > exit_threshold) {
                                neg_basis_scale_active_ = false;
                            }
                        }
                        else {
                            neg_basis_scale_active_ = (basis_pct < cfg_.neg_basis_threshold);
                        }

                        if (neg_basis_scale_active_) {
                            scale = std::min(scale, cfg_.neg_basis_scale);
                        }
                    }

                    if (cfg_.basis_overlay_cap > 0.0 && cfg_.basis_overlay_strength > 0.0) {
                        const double deviation = basis_pct - basis_level_ema_;
                        const double normalized = Clamp01(std::abs(deviation) / cfg_.basis_overlay_cap);
                        const double signed_dir = (deviation >= 0.0) ? 1.0 : -1.0;
                        const double overlay = 1.0 + signed_dir * normalized * cfg_.basis_overlay_strength;
                        scale = std::min(scale, std::max(0.0, overlay));
                    }

                    if (cfg_.basis_trend_max_abs > 0.0 && cfg_.basis_trend_max_abs < 1.0) {
                        const double slope = basis_level_ema_ - basis_level_ema_prev_;
                        if (perp_found) {
                            const double adverse_slope = perp_is_short ? slope : -slope;
                            if (adverse_slope > 0.0) {
                                const double raw_scale = 1.0 - (adverse_slope / cfg_.basis_trend_max_abs);
                                const double trend_scale = std::max(cfg_.basis_trend_min_scale, Clamp01(raw_scale));
                                scale = std::min(scale, trend_scale);
                            }
                        }
                    }
                }

                const double base_qty = (leg_notional_usdt * scale) / *spot_price;
                for (const auto& leg : intent.legs) {
                    const double price = price_by_symbol[leg.instrument];
                    const double sign = (leg.side == QTrading::Intent::TradeSide::Long) ? 1.0 : -1.0;
                    out.target_positions[leg.instrument] = base_qty * price * sign;
                    out.leverage[leg.instrument] = leverage_for_instrument_(leg.instrument);
                }
                return out;
            }
        }
    }

    for (const auto& leg : intent.legs) {
        const double signed_notional = (leg.side == QTrading::Intent::TradeSide::Long)
            ? leg_notional_usdt
            : -leg_notional_usdt;
        out.target_positions[leg.instrument] = signed_notional;
        out.leverage[leg.instrument] = leverage_for_instrument_(leg.instrument);
    }

    return out;
}

} // namespace QTrading::Risk
