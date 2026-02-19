#include "Execution/LiquidityAwareExecutionScheduler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <unordered_map>

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

LiquidityAwareExecutionScheduler::LiquidityAwareExecutionScheduler(Config cfg)
    : cfg_(cfg)
{
    OverrideBoolFromEnv(
        "QTR_FC_EXEC_SCHED_ENABLE_DELTA_PART_CAP",
        cfg_.carry_delta_participation_cap_enabled);
    OverrideDoubleFromEnv(
        "QTR_FC_EXEC_SCHED_DELTA_PART_RATE",
        cfg_.carry_delta_participation_rate);
    OverrideDoubleFromEnv(
        "QTR_FC_EXEC_SCHED_MIN_SLICE_NOTIONAL",
        cfg_.carry_min_slice_notional_usdt);
    OverrideBoolFromEnv(
        "QTR_FC_EXEC_SCHED_ONLY_LOW_URGENCY",
        cfg_.carry_apply_only_low_urgency);
    OverrideBoolFromEnv(
        "QTR_FC_EXEC_SCHED_INCLUDE_OPEN_ORDERS",
        cfg_.include_open_orders_in_current_notional);
    OverrideBoolFromEnv(
        "QTR_FC_EXEC_SCHED_CONF_ADAPT_ENABLE",
        cfg_.carry_confidence_adaptive_enabled);
    OverrideDoubleFromEnv(
        "QTR_FC_EXEC_SCHED_CONF_RATE_SCALE_MIN",
        cfg_.carry_confidence_rate_scale_min);
    OverrideDoubleFromEnv(
        "QTR_FC_EXEC_SCHED_CONF_RATE_SCALE_MAX",
        cfg_.carry_confidence_rate_scale_max);
    OverrideBoolFromEnv(
        "QTR_FC_EXEC_SCHED_GAP_ADAPT_ENABLE",
        cfg_.carry_gap_adaptive_enabled);
    OverrideDoubleFromEnv(
        "QTR_FC_EXEC_SCHED_GAP_REFERENCE_RATIO",
        cfg_.carry_gap_reference_ratio);
    OverrideDoubleFromEnv(
        "QTR_FC_EXEC_SCHED_GAP_RATE_SCALE_MIN",
        cfg_.carry_gap_rate_scale_min);
    OverrideDoubleFromEnv(
        "QTR_FC_EXEC_SCHED_GAP_RATE_SCALE_MAX",
        cfg_.carry_gap_rate_scale_max);
    OverrideBoolFromEnv(
        "QTR_FC_EXEC_SCHED_WINDOW_BUDGET_ENABLE",
        cfg_.carry_window_budget_enabled);
    OverrideUint64FromEnv(
        "QTR_FC_EXEC_SCHED_WINDOW_BUDGET_MS",
        cfg_.carry_window_budget_ms);
    OverrideDoubleFromEnv(
        "QTR_FC_EXEC_SCHED_WINDOW_BUDGET_PART_RATE",
        cfg_.carry_window_quote_participation_rate);
    OverrideDoubleFromEnv(
        "QTR_FC_EXEC_SCHED_WINDOW_BUDGET_MAX_NOTIONAL",
        cfg_.carry_window_max_notional_usdt);
    OverrideBoolFromEnv(
        "QTR_FC_EXEC_SCHED_WINDOW_BUDGET_CONF_ADAPT_ENABLE",
        cfg_.carry_window_confidence_adaptive_enabled);
    OverrideDoubleFromEnv(
        "QTR_FC_EXEC_SCHED_WINDOW_BUDGET_CONF_SCALE_MIN",
        cfg_.carry_window_confidence_scale_min);
    OverrideDoubleFromEnv(
        "QTR_FC_EXEC_SCHED_WINDOW_BUDGET_CONF_SCALE_MAX",
        cfg_.carry_window_confidence_scale_max);
    OverrideBoolFromEnv(
        "QTR_FC_EXEC_SCHED_INCREASE_BATCH_ENABLE",
        cfg_.carry_increase_batching_enabled);
    OverrideUint64FromEnv(
        "QTR_FC_EXEC_SCHED_INCREASE_BATCH_MS",
        cfg_.carry_increase_batch_ms);
    OverrideDoubleFromEnv(
        "QTR_FC_EXEC_SCHED_INCREASE_BATCH_MIN_UPDATE_NOTIONAL",
        cfg_.carry_increase_batch_min_update_notional);
    OverrideDoubleFromEnv(
        "QTR_FC_EXEC_SCHED_INCREASE_BATCH_MIN_UPDATE_RATIO",
        cfg_.carry_increase_batch_min_update_ratio);

    cfg_.carry_delta_participation_rate =
        ClampNonNegative(cfg_.carry_delta_participation_rate);
    cfg_.carry_min_slice_notional_usdt =
        ClampNonNegative(cfg_.carry_min_slice_notional_usdt);
    cfg_.carry_confidence_rate_scale_min =
        ClampNonNegative(cfg_.carry_confidence_rate_scale_min);
    cfg_.carry_confidence_rate_scale_max =
        ClampNonNegative(cfg_.carry_confidence_rate_scale_max);
    if (cfg_.carry_confidence_rate_scale_max < cfg_.carry_confidence_rate_scale_min) {
        cfg_.carry_confidence_rate_scale_max = cfg_.carry_confidence_rate_scale_min;
    }
    cfg_.carry_gap_reference_ratio =
        ClampNonNegative(cfg_.carry_gap_reference_ratio);
    cfg_.carry_gap_rate_scale_min =
        ClampNonNegative(cfg_.carry_gap_rate_scale_min);
    cfg_.carry_gap_rate_scale_max =
        ClampNonNegative(cfg_.carry_gap_rate_scale_max);
    if (cfg_.carry_gap_rate_scale_max < cfg_.carry_gap_rate_scale_min) {
        cfg_.carry_gap_rate_scale_max = cfg_.carry_gap_rate_scale_min;
    }
    if (cfg_.carry_window_budget_ms == 0) {
        cfg_.carry_window_budget_ms = 8ull * 60ull * 60ull * 1000ull;
    }
    cfg_.carry_window_quote_participation_rate =
        ClampNonNegative(cfg_.carry_window_quote_participation_rate);
    cfg_.carry_window_max_notional_usdt =
        ClampNonNegative(cfg_.carry_window_max_notional_usdt);
    cfg_.carry_window_confidence_scale_min =
        ClampNonNegative(cfg_.carry_window_confidence_scale_min);
    cfg_.carry_window_confidence_scale_max =
        ClampNonNegative(cfg_.carry_window_confidence_scale_max);
    if (cfg_.carry_window_confidence_scale_max < cfg_.carry_window_confidence_scale_min) {
        cfg_.carry_window_confidence_scale_max = cfg_.carry_window_confidence_scale_min;
    }
    if (cfg_.carry_increase_batch_ms == 0) {
        cfg_.carry_increase_batch_ms = 60ull * 60ull * 1000ull;
    }
    cfg_.carry_increase_batch_min_update_notional =
        ClampNonNegative(cfg_.carry_increase_batch_min_update_notional);
    cfg_.carry_increase_batch_min_update_ratio =
        ClampNonNegative(cfg_.carry_increase_batch_min_update_ratio);
}

std::vector<ExecutionSlice> LiquidityAwareExecutionScheduler::BuildSlices(
    const std::vector<ExecutionParentOrder>& parent_orders,
    const QTrading::Risk::AccountState& account,
    const QTrading::Signal::SignalDecision& signal,
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    std::vector<ExecutionSlice> slices;
    slices.reserve(parent_orders.size());

    if (!market) {
        return slices;
    }

    if (!has_symbol_index_ && market->symbols) {
        symbol_to_id_.clear();
        symbol_to_id_.reserve(market->symbols->size() * 2);
        for (std::size_t i = 0; i < market->symbols->size(); ++i) {
            symbol_to_id_[market->symbols->at(i)] = i;
        }
        has_symbol_index_ = !market->symbols->empty();
    }

    const bool apply_participation_cap =
        cfg_.carry_delta_participation_cap_enabled &&
        (cfg_.carry_delta_participation_rate > 0.0) &&
        (signal.strategy == "funding_carry") &&
        (!cfg_.carry_apply_only_low_urgency ||
         signal.urgency == QTrading::Signal::SignalUrgency::Low);
    const bool apply_window_budget =
        cfg_.carry_window_budget_enabled &&
        (signal.strategy == "funding_carry") &&
        (!cfg_.carry_apply_only_low_urgency ||
         signal.urgency == QTrading::Signal::SignalUrgency::Low);
    const bool apply_increase_batching =
        cfg_.carry_increase_batching_enabled &&
        (signal.strategy == "funding_carry") &&
        (!cfg_.carry_apply_only_low_urgency ||
         signal.urgency == QTrading::Signal::SignalUrgency::Low);

    if ((!apply_participation_cap && !apply_window_budget && !apply_increase_batching) ||
        !has_symbol_index_ ||
        market->klines_by_id.empty()) {
        for (const auto& parent : parent_orders) {
            slices.push_back(
                ExecutionSlice{
                    parent.ts_ms,
                    parent.symbol,
                    parent.target_notional,
                    parent.leverage,
                });
        }
        return slices;
    }

    std::unordered_map<std::string, double> current_notional_by_symbol;
    current_notional_by_symbol.reserve(parent_orders.size() * 2);
    for (const auto& position : account.positions) {
        const auto it = symbol_to_id_.find(position.symbol);
        if (it == symbol_to_id_.end()) {
            continue;
        }
        const double px = ClosePriceFromId(market, it->second);
        if (px <= 0.0) {
            continue;
        }
        const double sign = position.is_long ? 1.0 : -1.0;
        current_notional_by_symbol[position.symbol] += (position.quantity * px * sign);
    }

    if (cfg_.include_open_orders_in_current_notional) {
        for (const auto& order : account.open_orders) {
            const auto it = symbol_to_id_.find(order.symbol);
            if (it == symbol_to_id_.end()) {
                continue;
            }
            const double px = ClosePriceFromId(market, it->second);
            if (px <= 0.0) {
                continue;
            }
            const double sign = (order.side == QTrading::Dto::Trading::OrderSide::Buy)
                ? 1.0
                : -1.0;
            current_notional_by_symbol[order.symbol] += (order.quantity * px * sign);
        }
    }

    for (const auto& parent : parent_orders) {
        double parent_target_notional = parent.target_notional;
        if (apply_increase_batching) {
            auto target_it = batched_target_notional_by_symbol_.find(parent.symbol);
            auto ts_it = last_increase_batch_ts_by_symbol_.find(parent.symbol);
            if (target_it == batched_target_notional_by_symbol_.end() ||
                ts_it == last_increase_batch_ts_by_symbol_.end())
            {
                batched_target_notional_by_symbol_[parent.symbol] = parent.target_notional;
                const bool has_initial_increase = (std::fabs(parent.target_notional) > 1e-9);
                if (has_initial_increase || market->Timestamp < cfg_.carry_increase_batch_ms) {
                    last_increase_batch_ts_by_symbol_[parent.symbol] = market->Timestamp;
                }
                else {
                    last_increase_batch_ts_by_symbol_[parent.symbol] =
                        market->Timestamp - cfg_.carry_increase_batch_ms;
                }
            }
            else {
                const double batched_target = target_it->second;
                const double update_threshold = std::max(
                    cfg_.carry_increase_batch_min_update_notional,
                    std::fabs(parent.target_notional) * cfg_.carry_increase_batch_min_update_ratio);
                const double requested_change = std::fabs(parent.target_notional - batched_target);
                if (requested_change >= update_threshold) {
                    const double current_notional = current_notional_by_symbol[parent.symbol];
                    const double target_abs = std::fabs(parent.target_notional);
                    const double current_abs = std::fabs(current_notional);
                    const bool is_increase = (target_abs > current_abs + 1e-9);
                    if (!is_increase) {
                        target_it->second = parent.target_notional;
                    }
                    else if (market->Timestamp >= ts_it->second + cfg_.carry_increase_batch_ms) {
                        target_it->second = parent.target_notional;
                        ts_it->second = market->Timestamp;
                    }
                }
            }
            parent_target_notional = batched_target_notional_by_symbol_[parent.symbol];
        }

        double slice_target_notional = parent_target_notional;
        const auto id_it = symbol_to_id_.find(parent.symbol);
        if (id_it != symbol_to_id_.end()) {
            const double quote_volume = QuoteVolumeFromId(market, id_it->second);
            if (quote_volume > 0.0) {
                const double current_notional = current_notional_by_symbol[parent.symbol];
                const double raw_delta = parent_target_notional - current_notional;
                const double confidence = Clamp01(signal.confidence);
                double max_delta_notional = std::numeric_limits<double>::infinity();

                if (apply_participation_cap) {
                    double effective_rate = cfg_.carry_delta_participation_rate;

                    if (cfg_.carry_confidence_adaptive_enabled) {
                        const double conf_scale = Lerp(
                            cfg_.carry_confidence_rate_scale_min,
                            cfg_.carry_confidence_rate_scale_max,
                            confidence);
                        effective_rate *= conf_scale;
                    }
                    if (cfg_.carry_gap_adaptive_enabled && cfg_.carry_gap_reference_ratio > 0.0) {
                        const double target_abs = std::fabs(parent_target_notional);
                        if (target_abs > 0.0) {
                            const double gap_ratio = std::fabs(raw_delta) / target_abs;
                            const double gap_t = Clamp01(gap_ratio / cfg_.carry_gap_reference_ratio);
                            const double gap_scale = Lerp(
                                cfg_.carry_gap_rate_scale_min,
                                cfg_.carry_gap_rate_scale_max,
                                gap_t);
                            effective_rate *= gap_scale;
                        }
                    }
                    effective_rate = ClampNonNegative(effective_rate);
                    max_delta_notional = std::min(
                        max_delta_notional,
                        quote_volume * effective_rate);
                }

                if (apply_window_budget) {
                    const uint64_t window_key = market->Timestamp / cfg_.carry_window_budget_ms;
                    auto key_it = budget_window_key_by_symbol_.find(parent.symbol);
                    if (key_it == budget_window_key_by_symbol_.end() || key_it->second != window_key) {
                        budget_window_key_by_symbol_[parent.symbol] = window_key;
                        budget_consumed_notional_by_symbol_[parent.symbol] = 0.0;
                        budget_cumulative_quote_volume_by_symbol_[parent.symbol] = 0.0;
                    }
                    budget_cumulative_quote_volume_by_symbol_[parent.symbol] += quote_volume;

                    double window_cap = std::numeric_limits<double>::infinity();
                    if (cfg_.carry_window_quote_participation_rate > 0.0) {
                        window_cap = std::min(
                            window_cap,
                            budget_cumulative_quote_volume_by_symbol_[parent.symbol] *
                                cfg_.carry_window_quote_participation_rate);
                    }
                    if (cfg_.carry_window_max_notional_usdt > 0.0) {
                        window_cap = std::min(window_cap, cfg_.carry_window_max_notional_usdt);
                    }
                    if (cfg_.carry_window_confidence_adaptive_enabled &&
                        std::isfinite(window_cap)) {
                        const double conf_scale = Lerp(
                            cfg_.carry_window_confidence_scale_min,
                            cfg_.carry_window_confidence_scale_max,
                            confidence);
                        window_cap *= conf_scale;
                    }

                    if (std::isfinite(window_cap)) {
                        const double consumed = budget_consumed_notional_by_symbol_[parent.symbol];
                        const double remaining = std::max(0.0, window_cap - consumed);
                        max_delta_notional = std::min(max_delta_notional, remaining);
                    }
                }

                double clipped_delta = raw_delta;
                if (std::isfinite(max_delta_notional)) {
                    clipped_delta = std::clamp(raw_delta, -max_delta_notional, max_delta_notional);
                }

                if (std::fabs(clipped_delta) < cfg_.carry_min_slice_notional_usdt) {
                    clipped_delta = 0.0;
                }
                if (apply_window_budget) {
                    budget_consumed_notional_by_symbol_[parent.symbol] += std::fabs(clipped_delta);
                }
                slice_target_notional = current_notional + clipped_delta;
            }
        }

        slices.push_back(
            ExecutionSlice{
                parent.ts_ms,
                parent.symbol,
                slice_target_notional,
                parent.leverage,
            });
    }

    return slices;
}

} // namespace QTrading::Execution
