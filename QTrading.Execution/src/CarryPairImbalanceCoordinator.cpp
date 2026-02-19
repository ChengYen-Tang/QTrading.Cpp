#include "Execution/CarryPairImbalanceCoordinator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace QTrading::Execution {
namespace {

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
        // Keep code default on malformed env value.
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
        // Keep code default on malformed env value.
    }
}

double ClampNonNegative(double value)
{
    if (!std::isfinite(value) || value < 0.0) {
        return 0.0;
    }
    return value;
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

double Lerp(double lo, double hi, double t)
{
    return lo + (hi - lo) * Clamp01(t);
}

double ClosePriceFromId(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market,
    std::size_t id)
{
    if (!market || id >= market->klines_by_id.size()) {
        return 0.0;
    }
    const auto& opt = market->klines_by_id[id];
    if (!opt.has_value()) {
        return 0.0;
    }
    return std::max(0.0, opt->ClosePrice);
}

double QuoteVolumeFromId(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market,
    std::size_t id)
{
    if (!market || id >= market->klines_by_id.size()) {
        return 0.0;
    }
    const auto& opt = market->klines_by_id[id];
    if (!opt.has_value()) {
        return 0.0;
    }
    return std::max(0.0, opt->QuoteVolume);
}

} // namespace

CarryPairImbalanceCoordinator::CarryPairImbalanceCoordinator(Config cfg)
    : cfg_(cfg)
{
    OverrideBoolFromEnv("QTR_FC_EXEC_PAIR_COORD_ENABLE", cfg_.enabled);
    OverrideBoolFromEnv("QTR_FC_EXEC_PAIR_COORD_ONLY_CARRY", cfg_.apply_only_funding_carry);
    OverrideBoolFromEnv("QTR_FC_EXEC_PAIR_COORD_ONLY_LOW_URGENCY", cfg_.apply_only_low_urgency);
    OverrideBoolFromEnv("QTR_FC_EXEC_PAIR_COORD_IGNORE_REDUCE_ONLY", cfg_.ignore_reduce_only_orders);
    OverrideBoolFromEnv("QTR_FC_EXEC_PAIR_COORD_REQUIRE_TWO_SIDED", cfg_.require_two_sided);
    OverrideBoolFromEnv("QTR_FC_EXEC_PAIR_COORD_BALANCE_TWO_SIDED", cfg_.balance_two_sided);
    OverrideDoubleFromEnv("QTR_FC_EXEC_PAIR_COORD_MIN_NOTIONAL", cfg_.min_notional_usdt);
    OverrideDoubleFromEnv("QTR_FC_EXEC_PAIR_COORD_MAX_LEG_NOTIONAL", cfg_.max_leg_notional_usdt);
    OverrideBoolFromEnv("QTR_FC_EXEC_PAIR_COORD_CAP_BY_QV", cfg_.cap_by_quote_volume);
    OverrideDoubleFromEnv("QTR_FC_EXEC_PAIR_COORD_MAX_PART_RATE", cfg_.max_participation_rate);
    OverrideBoolFromEnv("QTR_FC_EXEC_PAIR_COORD_CONF_ADAPT_ENABLE", cfg_.carry_confidence_adaptive_enabled);
    OverrideDoubleFromEnv("QTR_FC_EXEC_PAIR_COORD_CONF_MAX_LEG_SCALE_MIN", cfg_.carry_confidence_max_leg_scale_min);
    OverrideDoubleFromEnv("QTR_FC_EXEC_PAIR_COORD_CONF_MAX_LEG_SCALE_MAX", cfg_.carry_confidence_max_leg_scale_max);
    OverrideDoubleFromEnv("QTR_FC_EXEC_PAIR_COORD_CONF_PART_SCALE_MIN", cfg_.carry_confidence_participation_scale_min);
    OverrideDoubleFromEnv("QTR_FC_EXEC_PAIR_COORD_CONF_PART_SCALE_MAX", cfg_.carry_confidence_participation_scale_max);

    cfg_.min_notional_usdt = ClampNonNegative(cfg_.min_notional_usdt);
    cfg_.max_leg_notional_usdt = ClampNonNegative(cfg_.max_leg_notional_usdt);
    cfg_.max_participation_rate = ClampNonNegative(cfg_.max_participation_rate);
    cfg_.carry_confidence_max_leg_scale_min = ClampNonNegative(cfg_.carry_confidence_max_leg_scale_min);
    cfg_.carry_confidence_max_leg_scale_max = ClampNonNegative(cfg_.carry_confidence_max_leg_scale_max);
    if (cfg_.carry_confidence_max_leg_scale_max < cfg_.carry_confidence_max_leg_scale_min) {
        cfg_.carry_confidence_max_leg_scale_max = cfg_.carry_confidence_max_leg_scale_min;
    }
    cfg_.carry_confidence_participation_scale_min = ClampNonNegative(cfg_.carry_confidence_participation_scale_min);
    cfg_.carry_confidence_participation_scale_max = ClampNonNegative(cfg_.carry_confidence_participation_scale_max);
    if (cfg_.carry_confidence_participation_scale_max < cfg_.carry_confidence_participation_scale_min) {
        cfg_.carry_confidence_participation_scale_max = cfg_.carry_confidence_participation_scale_min;
    }
}

std::vector<ExecutionOrder> CarryPairImbalanceCoordinator::Coordinate(
    std::vector<ExecutionOrder> orders,
    const QTrading::Signal::SignalDecision& signal,
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    if (!cfg_.enabled || !market) {
        return orders;
    }
    if (cfg_.apply_only_funding_carry && signal.strategy != "funding_carry") {
        return orders;
    }
    if (cfg_.apply_only_low_urgency && signal.urgency != QTrading::Signal::SignalUrgency::Low) {
        return orders;
    }
    if (orders.empty()) {
        return orders;
    }

    if (!has_symbol_index_ && market->symbols) {
        symbol_to_id_.clear();
        symbol_to_id_.reserve(market->symbols->size() * 2);
        for (std::size_t i = 0; i < market->symbols->size(); ++i) {
            symbol_to_id_[market->symbols->at(i)] = i;
        }
        has_symbol_index_ = !market->symbols->empty();
    }

    struct CarryOrderMeta {
        std::size_t index{ 0 };
        double signed_notional{ 0.0 };
        double reference_price{ 0.0 };
    };

    std::vector<CarryOrderMeta> carry_orders;
    carry_orders.reserve(orders.size());
    std::vector<bool> remove_order(orders.size(), false);
    const double confidence = Clamp01(signal.confidence);
    const double max_leg_scale = cfg_.carry_confidence_adaptive_enabled
        ? Lerp(
            cfg_.carry_confidence_max_leg_scale_min,
            cfg_.carry_confidence_max_leg_scale_max,
            confidence)
        : 1.0;
    const double part_scale = cfg_.carry_confidence_adaptive_enabled
        ? Lerp(
            cfg_.carry_confidence_participation_scale_min,
            cfg_.carry_confidence_participation_scale_max,
            confidence)
        : 1.0;
    const double effective_max_leg_notional = cfg_.max_leg_notional_usdt * max_leg_scale;
    const double effective_participation_rate = cfg_.max_participation_rate * part_scale;

    for (std::size_t i = 0; i < orders.size(); ++i) {
        auto& order = orders[i];
        if (cfg_.ignore_reduce_only_orders && order.reduce_only) {
            continue;
        }

        const auto id_it = symbol_to_id_.find(order.symbol);
        if (id_it == symbol_to_id_.end()) {
            continue;
        }
        const std::size_t symbol_id = id_it->second;
        const double px = ClosePriceFromId(market, symbol_id);
        if (px <= 0.0) {
            continue;
        }

        double notional = std::fabs(order.qty) * px;
        if (cfg_.cap_by_quote_volume && effective_participation_rate > 0.0) {
            const double qv = QuoteVolumeFromId(market, symbol_id);
            if (qv > 0.0) {
                const double qv_cap = qv * effective_participation_rate;
                notional = std::min(notional, qv_cap);
            }
        }
        if (effective_max_leg_notional > 0.0) {
            notional = std::min(notional, effective_max_leg_notional);
        }
        if (notional < cfg_.min_notional_usdt) {
            remove_order[i] = true;
            continue;
        }

        order.qty = notional / px;
        const double signed_notional = (order.action == OrderAction::Buy) ? notional : -notional;
        carry_orders.push_back(CarryOrderMeta{ i, signed_notional, px });
    }

    double total_buy_notional = 0.0;
    double total_sell_notional = 0.0;
    for (const auto& meta : carry_orders) {
        if (meta.signed_notional > 0.0) {
            total_buy_notional += meta.signed_notional;
        }
        else if (meta.signed_notional < 0.0) {
            total_sell_notional += -meta.signed_notional;
        }
    }

    if (cfg_.require_two_sided && (total_buy_notional <= 0.0 || total_sell_notional <= 0.0)) {
        for (const auto& meta : carry_orders) {
            remove_order[meta.index] = true;
        }
    }
    else if (cfg_.balance_two_sided && total_buy_notional > 0.0 && total_sell_notional > 0.0) {
        const bool scale_buy = total_buy_notional > total_sell_notional;
        const double larger = scale_buy ? total_buy_notional : total_sell_notional;
        const double smaller = scale_buy ? total_sell_notional : total_buy_notional;
        if (larger > 0.0 && smaller > 0.0) {
            const double scale = std::clamp(smaller / larger, 0.0, 1.0);
            if (scale < 0.999999) {
                for (const auto& meta : carry_orders) {
                    const bool is_buy = meta.signed_notional > 0.0;
                    if ((scale_buy && !is_buy) || (!scale_buy && is_buy)) {
                        continue;
                    }
                    auto& order = orders[meta.index];
                    const double adjusted_notional = std::fabs(meta.signed_notional) * scale;
                    if (adjusted_notional < cfg_.min_notional_usdt || meta.reference_price <= 0.0) {
                        remove_order[meta.index] = true;
                        continue;
                    }
                    order.qty = adjusted_notional / meta.reference_price;
                }
            }
        }
    }

    std::vector<ExecutionOrder> filtered;
    filtered.reserve(orders.size());
    for (std::size_t i = 0; i < orders.size(); ++i) {
        if (!remove_order[i]) {
            filtered.push_back(std::move(orders[i]));
        }
    }
    return filtered;
}

} // namespace QTrading::Execution
