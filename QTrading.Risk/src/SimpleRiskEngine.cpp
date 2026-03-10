#include "Risk/SimpleRiskEngine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <deque>

namespace {
constexpr double kAutoNotionalGrossDeviationFallbackRatio = 0.25;

std::optional<double> GetPriceBySymbol(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market,
    const std::string& symbol)
{
    if (!market) {
        return std::nullopt;
    }

    if (market->symbols && !market->trade_klines_by_id.empty()) {
        const auto& symbols = *market->symbols;
        for (std::size_t i = 0; i < symbols.size(); ++i) {
            if (symbols[i] == symbol && i < market->trade_klines_by_id.size()) {
                const auto& opt = market->trade_klines_by_id[i];
                if (opt.has_value()) {
                    return opt->ClosePrice;
                }
                return std::nullopt;
            }
        }
    }

    return std::nullopt;
}

std::optional<double> GetFundingRateBySymbol(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market,
    const std::string& symbol)
{
    if (!market || !market->symbols) {
        return std::nullopt;
    }
    const auto& symbols = *market->symbols;
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        if (symbols[i] != symbol) {
            continue;
        }
        if (i >= market->funding_by_id.size()) {
            return std::nullopt;
        }
        const auto& funding_opt = market->funding_by_id[i];
        if (!funding_opt.has_value()) {
            return std::nullopt;
        }
        return funding_opt->Rate;
    }
    return std::nullopt;
}

std::optional<QTrading::Dto::Market::Binance::FundingRateDto> GetFundingSnapshotBySymbol(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market,
    const std::string& symbol)
{
    if (!market || !market->symbols) {
        return std::nullopt;
    }
    const auto& symbols = *market->symbols;
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        if (symbols[i] != symbol) {
            continue;
        }
        if (i >= market->funding_by_id.size()) {
            return std::nullopt;
        }
        const auto& funding_opt = market->funding_by_id[i];
        if (!funding_opt.has_value()) {
            return std::nullopt;
        }
        return *funding_opt;
    }
    return std::nullopt;
}

double SignedNotionalFromPosition(const QTrading::dto::Position& pos, double price)
{
    const double sign = pos.is_long ? 1.0 : -1.0;
    return pos.quantity * price * sign;
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

double ClampPositive(double value, double fallback)
{
    if (!std::isfinite(value) || value <= 0.0) {
        return fallback;
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

double MaxLegCap(const QTrading::Risk::SimpleRiskEngine::Config& cfg)
{
    if (std::isfinite(cfg.max_leg_notional_usdt) && (cfg.max_leg_notional_usdt > 0.0)) {
        return cfg.max_leg_notional_usdt;
    }
    return std::numeric_limits<double>::infinity();
}

} // namespace

namespace QTrading::Risk {

SimpleRiskEngine::SimpleRiskEngine(Config cfg)
    : cfg_(cfg)
{
    cfg_.min_notional_scale = Clamp01(cfg_.min_notional_scale);
    cfg_.basis_trend_min_scale = Clamp01(cfg_.basis_trend_min_scale);
    cfg_.basis_trend_ema_alpha = ClampAlpha(cfg_.basis_trend_ema_alpha);
    cfg_.neg_basis_scale = Clamp01(cfg_.neg_basis_scale);
    cfg_.neg_basis_hysteresis_pct = ClampNonNegative(cfg_.neg_basis_hysteresis_pct);
    cfg_.basis_overlay_strength = Clamp01(cfg_.basis_overlay_strength);
    cfg_.basis_overlay_upscale_cap = ClampPositive(cfg_.basis_overlay_upscale_cap, 1.0);
    cfg_.basis_overlay_downscale_floor = Clamp01(cfg_.basis_overlay_downscale_floor);
    cfg_.basis_overlay_activation_ratio = Clamp01(cfg_.basis_overlay_activation_ratio);
    if (cfg_.basis_overlay_upscale_cap < 1.0) {
        cfg_.basis_overlay_upscale_cap = 1.0;
    }
    if (cfg_.basis_overlay_downscale_floor > cfg_.basis_overlay_upscale_cap) {
        cfg_.basis_overlay_downscale_floor = cfg_.basis_overlay_upscale_cap;
    }
    cfg_.basis_level_ema_alpha = ClampAlpha(cfg_.basis_level_ema_alpha);
    cfg_.basis_alpha_overlay_band_pct = ClampPositive(cfg_.basis_alpha_overlay_band_pct, 0.01);
    cfg_.basis_alpha_overlay_upscale_cap = ClampPositive(cfg_.basis_alpha_overlay_upscale_cap, 1.0);
    if (cfg_.basis_alpha_overlay_upscale_cap < 1.0) {
        cfg_.basis_alpha_overlay_upscale_cap = 1.0;
    }
    cfg_.basis_alpha_overlay_downscale_floor = Clamp01(cfg_.basis_alpha_overlay_downscale_floor);
    if (cfg_.basis_alpha_overlay_downscale_floor <= 0.0) {
        cfg_.basis_alpha_overlay_downscale_floor = 0.01;
    }
    // Backward compatibility bridge:
    // If caller still uses the legacy fixed threshold only, map it to low/high anchors.
    if (cfg_.carry_size_min_gain_to_cost_low_confidence == 1.0 &&
        cfg_.carry_size_min_gain_to_cost_high_confidence == 1.0 &&
        cfg_.carry_size_min_gain_to_cost != 1.0)
    {
        cfg_.carry_size_min_gain_to_cost_low_confidence = cfg_.carry_size_min_gain_to_cost;
        cfg_.carry_size_min_gain_to_cost_high_confidence = cfg_.carry_size_min_gain_to_cost;
    }
    cfg_.carry_confidence_min_scale = Clamp01(cfg_.carry_confidence_min_scale);
    cfg_.carry_confidence_max_scale = ClampPositive(cfg_.carry_confidence_max_scale, 1.0);
    if (cfg_.carry_confidence_max_scale < cfg_.carry_confidence_min_scale) {
        cfg_.carry_confidence_max_scale = cfg_.carry_confidence_min_scale;
    }
    cfg_.carry_confidence_power = ClampPositive(cfg_.carry_confidence_power, 1.0);
    cfg_.carry_core_notional_ratio = Clamp01(cfg_.carry_core_notional_ratio);
    cfg_.carry_overlay_notional_ratio = ClampNonNegative(cfg_.carry_overlay_notional_ratio);
    cfg_.carry_overlay_confidence_power = ClampPositive(cfg_.carry_overlay_confidence_power, 1.0);
    cfg_.carry_confidence_min_leverage_scale = ClampPositive(cfg_.carry_confidence_min_leverage_scale, 1.0);
    cfg_.carry_confidence_max_leverage_scale = ClampPositive(cfg_.carry_confidence_max_leverage_scale, 1.0);
    if (cfg_.carry_confidence_max_leverage_scale < cfg_.carry_confidence_min_leverage_scale) {
        cfg_.carry_confidence_max_leverage_scale = cfg_.carry_confidence_min_leverage_scale;
    }
    cfg_.carry_confidence_leverage_power = ClampPositive(cfg_.carry_confidence_leverage_power, 1.0);
    cfg_.carry_confidence_boost_reference = Clamp01(cfg_.carry_confidence_boost_reference);
    cfg_.carry_confidence_boost_max_scale = ClampNonNegative(cfg_.carry_confidence_boost_max_scale);
    cfg_.carry_confidence_boost_power = ClampPositive(cfg_.carry_confidence_boost_power, 1.0);
    cfg_.carry_confidence_boost_regime_window_settlements = std::max<std::size_t>(
        1,
        cfg_.carry_confidence_boost_regime_window_settlements);
    cfg_.carry_confidence_boost_regime_min_samples = std::max<std::size_t>(
        1,
        cfg_.carry_confidence_boost_regime_min_samples);
    cfg_.carry_confidence_boost_regime_negative_share_weight =
        Clamp01(cfg_.carry_confidence_boost_regime_negative_share_weight);
    cfg_.carry_confidence_boost_regime_floor_scale =
        Clamp01(cfg_.carry_confidence_boost_regime_floor_scale);
    cfg_.carry_size_cost_rate_per_leg = ClampNonNegative(cfg_.carry_size_cost_rate_per_leg);
    cfg_.carry_size_expected_hold_settlements = ClampNonNegative(cfg_.carry_size_expected_hold_settlements);
    cfg_.carry_size_min_gain_to_cost = ClampPositive(cfg_.carry_size_min_gain_to_cost, 1.0);
    cfg_.carry_size_min_gain_to_cost_low_confidence =
        ClampPositive(cfg_.carry_size_min_gain_to_cost_low_confidence, cfg_.carry_size_min_gain_to_cost);
    cfg_.carry_size_min_gain_to_cost_high_confidence =
        ClampPositive(cfg_.carry_size_min_gain_to_cost_high_confidence, cfg_.carry_size_min_gain_to_cost);
    cfg_.carry_size_gain_to_cost_confidence_power =
        ClampPositive(cfg_.carry_size_gain_to_cost_confidence_power, 1.0);
    cfg_.carry_allocator_spot_cash_per_notional =
        ClampPositive(cfg_.carry_allocator_spot_cash_per_notional, 1.0);
    cfg_.carry_allocator_perp_margin_buffer_ratio =
        ClampNonNegative(cfg_.carry_allocator_perp_margin_buffer_ratio);
    cfg_.carry_allocator_perp_leverage =
        ClampNonNegative(cfg_.carry_allocator_perp_leverage);
    cfg_.dual_ledger_auto_notional_ratio = ClampNonNegative(cfg_.dual_ledger_auto_notional_ratio);
    cfg_.dual_ledger_auto_notional_ema_alpha = ClampAlpha(cfg_.dual_ledger_auto_notional_ema_alpha);
    cfg_.dual_ledger_spot_available_usage = Clamp01(cfg_.dual_ledger_spot_available_usage);
    cfg_.dual_ledger_perp_available_usage = Clamp01(cfg_.dual_ledger_perp_available_usage);
    cfg_.perp_liq_buffer_floor_ratio = ClampNonNegative(cfg_.perp_liq_buffer_floor_ratio);
    cfg_.perp_liq_buffer_ceiling_ratio = ClampNonNegative(cfg_.perp_liq_buffer_ceiling_ratio);
    if (cfg_.perp_liq_buffer_ceiling_ratio <= cfg_.perp_liq_buffer_floor_ratio) {
        cfg_.perp_liq_buffer_ceiling_ratio = cfg_.perp_liq_buffer_floor_ratio + 1.0;
    }
    cfg_.perp_liq_min_notional_scale = Clamp01(cfg_.perp_liq_min_notional_scale);

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

double SimpleRiskEngine::leverage_for_instrument_scaled_(const std::string& instrument, double scale) const
{
    const auto& spec = instrument_registry_.Resolve(instrument);
    if (spec.max_leverage <= 1.0) {
        return 1.0;
    }

    const double base = leverage_for_instrument_(instrument);
    const double safe_scale = ClampPositive(scale, 1.0);
    return std::min(base * safe_scale, spec.max_leverage);
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
    const double configured_leg_notional = EffectiveLegNotional(cfg_);
    const double carry_confidence = Clamp01(intent.confidence);
    const double confidence_notional_scale =
        cfg_.carry_confidence_min_scale +
        (cfg_.carry_confidence_max_scale - cfg_.carry_confidence_min_scale) *
        std::pow(carry_confidence, cfg_.carry_confidence_power);
    const double confidence_leverage_scale =
        cfg_.carry_confidence_min_leverage_scale +
        (cfg_.carry_confidence_max_leverage_scale - cfg_.carry_confidence_min_leverage_scale) *
        std::pow(carry_confidence, cfg_.carry_confidence_leverage_power);

    if (is_carry && market && intent.legs.size() >= 2) {
        double gross = 0.0;
        double net = 0.0;
        bool perp_is_short = false;
        bool perp_found = false;
        std::optional<double> spot_price_snapshot;
        std::optional<double> perp_price_snapshot;
        std::string perp_symbol;
        std::unordered_map<std::string, double> current_notional;
        current_notional.reserve(intent.legs.size());
        std::unordered_map<std::string, double> price_by_symbol;
        price_by_symbol.reserve(intent.legs.size());

        for (const auto& leg : intent.legs) {
            if (is_perp_instrument_(leg.instrument)) {
                perp_found = true;
                perp_is_short = (leg.side == QTrading::Intent::TradeSide::Short);
                if (perp_symbol.empty()) {
                    perp_symbol = leg.instrument;
                }
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
        }

        std::optional<double> spot_price = spot_price_snapshot;
        std::optional<double> perp_price = perp_price_snapshot;
        if (!spot_price.has_value() && !price_by_symbol.empty()) {
            spot_price = price_by_symbol.begin()->second;
        }

        double dynamic_target_leg_notional = configured_leg_notional;
        bool has_dynamic_target_leg_notional = false;
        double effective_boost_max_scale = cfg_.carry_confidence_boost_max_scale;
        if (cfg_.carry_confidence_boost_regime_aware_enabled && !perp_symbol.empty()) {
            const auto funding_snapshot = GetFundingSnapshotBySymbol(market, perp_symbol);
            if (funding_snapshot.has_value()) {
                const uint64_t funding_time = funding_snapshot->FundingTime;
                const bool settlement_advanced =
                    (!has_last_boost_regime_funding_time_) ||
                    (funding_time > last_boost_regime_funding_time_);
                if (settlement_advanced) {
                    has_last_boost_regime_funding_time_ = true;
                    last_boost_regime_funding_time_ = funding_time;
                    const int sign = (funding_snapshot->Rate < 0.0) ? -1 : 1;
                    boost_regime_funding_sign_history_.push_back(sign);
                    while (boost_regime_funding_sign_history_.size() >
                        cfg_.carry_confidence_boost_regime_window_settlements)
                    {
                        boost_regime_funding_sign_history_.pop_front();
                    }
                }
            }

            if (boost_regime_funding_sign_history_.size() >=
                cfg_.carry_confidence_boost_regime_min_samples)
            {
                std::size_t negative_count = 0;
                for (const int sign : boost_regime_funding_sign_history_) {
                    if (sign < 0) {
                        ++negative_count;
                    }
                }
                const double negative_share = static_cast<double>(negative_count) /
                    static_cast<double>(boost_regime_funding_sign_history_.size());
                const double regime_scale = std::max(
                    cfg_.carry_confidence_boost_regime_floor_scale,
                    1.0 - cfg_.carry_confidence_boost_regime_negative_share_weight * negative_share);
                effective_boost_max_scale *= Clamp01(regime_scale);
            }
        }
        if (spot_price.has_value() && *spot_price > 0.0) {
            // Build the same carry target that execution sizing will use later.
            // This avoids net-neutral early-return freezing rebalances when
            // auto-notional has grown far beyond the static configured notional.
            double target_leg_notional = configured_leg_notional;
            const double leg_cap = MaxLegCap(cfg_);

            if (account.total_cash_balance.has_value() &&
                std::isfinite(*account.total_cash_balance) &&
                *account.total_cash_balance > 0.0)
            {
                bool has_raw_auto_target = false;
                double raw_auto_target = 0.0;

                if (cfg_.carry_allocator_leverage_model_enabled) {
                    double allocator_leverage = cfg_.carry_allocator_perp_leverage;
                    if (allocator_leverage <= 0.0) {
                        if (!perp_symbol.empty()) {
                            allocator_leverage =
                                leverage_for_instrument_scaled_(
                                    perp_symbol,
                                    confidence_leverage_scale);
                        }
                        else {
                            allocator_leverage = cfg_.leverage;
                        }
                    }
                    allocator_leverage = std::max(1.0, allocator_leverage);

                    const double denom =
                        cfg_.carry_allocator_spot_cash_per_notional +
                        (1.0 / allocator_leverage) +
                        cfg_.carry_allocator_perp_margin_buffer_ratio;
                    if (denom > 0.0) {
                        raw_auto_target = *account.total_cash_balance / denom;
                        has_raw_auto_target = true;
                    }
                }

                if (!has_raw_auto_target &&
                    cfg_.dual_ledger_auto_notional_ratio > 0.0) {
                    raw_auto_target =
                        *account.total_cash_balance * cfg_.dual_ledger_auto_notional_ratio;
                    has_raw_auto_target = true;
                }

                if (has_raw_auto_target) {
                    if (!auto_notional_ema_initialized_) {
                        auto_notional_ema_ = raw_auto_target;
                        auto_notional_ema_initialized_ = true;
                    }
                    else {
                        const double alpha = cfg_.dual_ledger_auto_notional_ema_alpha;
                        auto_notional_ema_ =
                            auto_notional_ema_ * (1.0 - alpha) + raw_auto_target * alpha;
                    }
                    target_leg_notional = std::max(target_leg_notional, auto_notional_ema_);
                }
            }

            if (account.spot_balance.has_value()) {
                double spot_capacity = std::numeric_limits<double>::infinity();
                const double spot_available = std::max(0.0, account.spot_balance->AvailableBalance);
                for (const auto& leg : intent.legs) {
                    if (!is_spot_instrument_(leg.instrument)) {
                        continue;
                    }
                    const auto nit = current_notional.find(leg.instrument);
                    const double current_abs = (nit == current_notional.end())
                        ? 0.0
                        : std::abs(nit->second);
                    const double capacity = current_abs + spot_available * cfg_.dual_ledger_spot_available_usage;
                    spot_capacity = std::min(spot_capacity, capacity);
                }
                target_leg_notional = std::min(target_leg_notional, spot_capacity);
            }

            if (account.perp_balance.has_value()) {
                double perp_capacity = std::numeric_limits<double>::infinity();
                const double perp_available = std::max(0.0, account.perp_balance->AvailableBalance);
                for (const auto& leg : intent.legs) {
                    if (!is_perp_instrument_(leg.instrument)) {
                        continue;
                    }
                    const auto nit = current_notional.find(leg.instrument);
                    const double current_abs = (nit == current_notional.end())
                        ? 0.0
                        : std::abs(nit->second);
                    const double lev = std::max(
                        1.0,
                        leverage_for_instrument_scaled_(leg.instrument, confidence_leverage_scale));
                    const double capacity = current_abs + perp_available * lev * cfg_.dual_ledger_perp_available_usage;
                    perp_capacity = std::min(perp_capacity, capacity);
                }
                target_leg_notional = std::min(target_leg_notional, perp_capacity);
            }

            if (cfg_.perp_liq_buffer_floor_ratio > 0.0 && account.perp_balance.has_value()) {
                const double margin_balance = account.perp_balance->MarginBalance;
                const double maintenance_margin = account.perp_balance->MaintenanceMargin;
                if (std::isfinite(margin_balance) &&
                    std::isfinite(maintenance_margin) &&
                    margin_balance > 0.0 &&
                    maintenance_margin > 0.0)
                {
                    const double buffer_ratio =
                        (margin_balance - maintenance_margin) / maintenance_margin;
                    const double floor = cfg_.perp_liq_buffer_floor_ratio;
                    const double ceiling = cfg_.perp_liq_buffer_ceiling_ratio;

                    double liq_scale = 1.0;
                    if (buffer_ratio <= floor) {
                        liq_scale = cfg_.perp_liq_min_notional_scale;
                    }
                    else if (buffer_ratio < ceiling) {
                        const double progress = (buffer_ratio - floor) / (ceiling - floor);
                        liq_scale =
                            cfg_.perp_liq_min_notional_scale +
                            (1.0 - cfg_.perp_liq_min_notional_scale) * progress;
                    }
                    target_leg_notional *= Clamp01(liq_scale);
                }
            }

            target_leg_notional = std::min(target_leg_notional, leg_cap);
            target_leg_notional = std::max(0.0, target_leg_notional);
            if (cfg_.carry_core_overlay_enabled) {
                const double overlay_confidence =
                    std::pow(carry_confidence, cfg_.carry_overlay_confidence_power);
                const double model_scale =
                    cfg_.carry_core_notional_ratio +
                    cfg_.carry_overlay_notional_ratio * overlay_confidence;
                target_leg_notional *= std::max(0.0, model_scale);
            }
            else {
                target_leg_notional *= confidence_notional_scale;
            }
            if (cfg_.carry_confidence_boost_enabled &&
                effective_boost_max_scale > 0.0 &&
                cfg_.carry_confidence_boost_reference < 1.0)
            {
                const double ref = cfg_.carry_confidence_boost_reference;
                const double normalized =
                    Clamp01((carry_confidence - ref) / std::max(1.0 - ref, 1e-9));
                const double shaped =
                    std::pow(normalized, cfg_.carry_confidence_boost_power);
                const double boost_scale =
                    1.0 + effective_boost_max_scale * shaped;
                target_leg_notional *= boost_scale;
            }
            target_leg_notional = std::min(target_leg_notional, leg_cap);

            dynamic_target_leg_notional = target_leg_notional;
            has_dynamic_target_leg_notional = true;
        }

        bool gross_deviation_exceeded = false;
        const double gross_reference_leg_notional =
            has_dynamic_target_leg_notional ? dynamic_target_leg_notional : configured_leg_notional;
        double effective_gross_deviation_trigger_ratio = cfg_.gross_deviation_trigger_ratio;
        if (!std::isfinite(effective_gross_deviation_trigger_ratio) &&
            (cfg_.dual_ledger_auto_notional_ratio > 0.0 ||
             cfg_.carry_allocator_leverage_model_enabled))
        {
            // In auto-notional mode, disabling gross trigger can freeze rebalancing for
            // long periods once net exposure stays near neutral.
            effective_gross_deviation_trigger_ratio = kAutoNotionalGrossDeviationFallbackRatio;
        }
        if (gross > 0.0 &&
            (gross_reference_leg_notional >= cfg_.gross_deviation_trigger_notional_threshold) &&
            std::isfinite(effective_gross_deviation_trigger_ratio) &&
            effective_gross_deviation_trigger_ratio >= 0.0)
        {
            const double target_gross = gross_reference_leg_notional * static_cast<double>(intent.legs.size());
            if (target_gross > 0.0) {
                const double deviation = std::abs(gross - target_gross) / target_gross;
                gross_deviation_exceeded = (deviation >= effective_gross_deviation_trigger_ratio);
            }
        }

        if (!current_notional.empty() && gross > 0.0 && !gross_deviation_exceeded) {
            const double ratio = std::abs(net) / gross;
            if (ratio < cfg_.rebalance_threshold_ratio) {
                for (const auto& leg : intent.legs) {
                    out.target_positions[leg.instrument] = current_notional[leg.instrument];
                    if (is_perp_instrument_(leg.instrument)) {
                        out.leverage[leg.instrument] =
                            leverage_for_instrument_scaled_(leg.instrument, confidence_leverage_scale);
                    }
                    else {
                        out.leverage[leg.instrument] = leverage_for_instrument_(leg.instrument);
                    }
                }
                return out;
            }
        }

        if (spot_price.has_value() && *spot_price > 0.0) {
            double target_leg_notional =
                has_dynamic_target_leg_notional ? dynamic_target_leg_notional : configured_leg_notional;
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
                    const bool refresh_overlay =
                        !basis_overlay_multiplier_initialized_ ||
                        cfg_.basis_overlay_refresh_ms == 0 ||
                        market->Timestamp >= (basis_overlay_last_refresh_ts_ + cfg_.basis_overlay_refresh_ms);
                    if (refresh_overlay) {
                        const double deviation = basis_pct - basis_level_ema_;
                        const double activation_abs =
                            cfg_.basis_overlay_cap * cfg_.basis_overlay_activation_ratio;
                        double overlay = 1.0;
                        if (std::abs(deviation) >= activation_abs) {
                            const double normalized = Clamp01(std::abs(deviation) / cfg_.basis_overlay_cap);
                            const double signed_dir = (deviation >= 0.0) ? 1.0 : -1.0;
                            overlay = 1.0 + signed_dir * normalized * cfg_.basis_overlay_strength;
                        }
                        if (!cfg_.basis_overlay_allow_upscale && overlay > 1.0) {
                            overlay = 1.0;
                        }
                        overlay = std::max(cfg_.basis_overlay_downscale_floor, overlay);
                        overlay = std::min(cfg_.basis_overlay_upscale_cap, overlay);
                        basis_overlay_multiplier_ = overlay;
                        basis_overlay_multiplier_initialized_ = true;
                        basis_overlay_last_refresh_ts_ = market->Timestamp;
                    }
                    scale *= basis_overlay_multiplier_;
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

                if (intent.strategy == "basis_arbitrage" && cfg_.basis_alpha_overlay_enabled) {
                    const double centered_basis = basis_pct - cfg_.basis_alpha_overlay_center_pct;
                    const double normalized = std::clamp(
                        centered_basis / cfg_.basis_alpha_overlay_band_pct,
                        -1.0,
                        1.0);
                    // For default receive-funding carry structure:
                    // long spot + short perp prefers positive basis (perp rich to spot).
                    const double directional = perp_is_short ? normalized : -normalized;
                    double directional_scale = 1.0;
                    if (directional >= 0.0) {
                        directional_scale =
                            1.0 + directional * (cfg_.basis_alpha_overlay_upscale_cap - 1.0);
                    }
                    else {
                        directional_scale =
                            1.0 + directional * (1.0 - cfg_.basis_alpha_overlay_downscale_floor);
                    }
                    directional_scale = std::clamp(
                        directional_scale,
                        cfg_.basis_alpha_overlay_downscale_floor,
                        cfg_.basis_alpha_overlay_upscale_cap);
                    scale *= directional_scale;
                }
            }

            const double base_leg_notional = target_leg_notional * scale;
            const bool enforce_notional_parity = (intent.strategy == "basis_arbitrage");
            const double base_qty = base_leg_notional / *spot_price;
            for (const auto& leg : intent.legs) {
                const double price = price_by_symbol[leg.instrument];
                const double sign = (leg.side == QTrading::Intent::TradeSide::Long) ? 1.0 : -1.0;
                if (enforce_notional_parity) {
                    // Basis-arbitrage mode enforces same absolute notional on both legs.
                    // This avoids systematic gross/net drift when spot/perp prices diverge.
                    out.target_positions[leg.instrument] = base_leg_notional * sign;
                }
                else {
                    out.target_positions[leg.instrument] = base_qty * price * sign;
                }
                if (is_perp_instrument_(leg.instrument)) {
                    out.leverage[leg.instrument] =
                        leverage_for_instrument_scaled_(leg.instrument, confidence_leverage_scale);
                }
                else {
                    out.leverage[leg.instrument] = leverage_for_instrument_(leg.instrument);
                }
            }

            const bool economics_gate_enabled =
                (cfg_.carry_size_cost_rate_per_leg > 0.0) &&
                (cfg_.carry_size_expected_hold_settlements > 0.0) &&
                !current_notional.empty();
            if (economics_gate_enabled) {
                std::string perp_symbol;
                for (const auto& leg : intent.legs) {
                    if (is_perp_instrument_(leg.instrument)) {
                        perp_symbol = leg.instrument;
                        break;
                    }
                }
                if (!perp_symbol.empty()) {
                    const auto observed_rate = GetFundingRateBySymbol(market, perp_symbol);
                    if (observed_rate.has_value() && std::isfinite(*observed_rate)) {
                        const double funding_rate_abs = std::abs(*observed_rate);
                        if (funding_rate_abs > 0.0) {
                            double delta_notional_total = 0.0;
                            for (const auto& leg : intent.legs) {
                                const auto current_it = current_notional.find(leg.instrument);
                                const double current_abs =
                                    (current_it == current_notional.end())
                                    ? 0.0
                                    : std::abs(current_it->second);
                                const auto target_it = out.target_positions.find(leg.instrument);
                                const double target_abs =
                                    (target_it == out.target_positions.end())
                                    ? current_abs
                                    : std::abs(target_it->second);
                                delta_notional_total += std::abs(target_abs - current_abs);
                            }

                            const double estimated_cost =
                                delta_notional_total * cfg_.carry_size_cost_rate_per_leg;
                            const double estimated_gain =
                                delta_notional_total *
                                funding_rate_abs *
                                cfg_.carry_size_expected_hold_settlements;
                            const double gain_to_cost_ratio_required =
                                cfg_.carry_size_min_gain_to_cost_low_confidence +
                                (cfg_.carry_size_min_gain_to_cost_high_confidence -
                                    cfg_.carry_size_min_gain_to_cost_low_confidence) *
                                    std::pow(carry_confidence, cfg_.carry_size_gain_to_cost_confidence_power);
                            const double required_gain =
                                estimated_cost * gain_to_cost_ratio_required;

                            if (estimated_gain < required_gain) {
                                for (const auto& leg : intent.legs) {
                                    out.target_positions[leg.instrument] = current_notional[leg.instrument];
                                    if (is_perp_instrument_(leg.instrument)) {
                                        out.leverage[leg.instrument] =
                                            leverage_for_instrument_scaled_(leg.instrument, confidence_leverage_scale);
                                    }
                                    else {
                                        out.leverage[leg.instrument] = leverage_for_instrument_(leg.instrument);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            return out;
        }
    }

    for (const auto& leg : intent.legs) {
        const double signed_notional = (leg.side == QTrading::Intent::TradeSide::Long)
            ? configured_leg_notional
            : -configured_leg_notional;
        out.target_positions[leg.instrument] = signed_notional;
        out.leverage[leg.instrument] = leverage_for_instrument_(leg.instrument);
    }

    return out;
}

} // namespace QTrading::Risk
