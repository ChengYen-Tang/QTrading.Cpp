#include "Execution/LiquidityAwareExecutionScheduler.hpp"

#include "Contracts/StrategyIdentity.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace QTrading::Execution {
namespace {

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
    if (!market || id >= market->trade_klines_by_id.size()) {
        return 0.0;
    }
    const auto& opt = market->trade_klines_by_id[id];
    if (!opt.has_value()) {
        return 0.0;
    }
    return std::max(0.0, opt->ClosePrice);
}

double QuoteVolumeFromId(
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market,
    std::size_t id)
{
    if (!market || id >= market->trade_klines_by_id.size()) {
        return 0.0;
    }
    const auto& opt = market->trade_klines_by_id[id];
    if (!opt.has_value()) {
        return 0.0;
    }
    return std::max(0.0, opt->QuoteVolume);
}

bool IsCarryLikeStrategy(const QTrading::Execution::ExecutionSignal& signal)
{
    const auto kind =
        QTrading::Contracts::ResolveStrategyKind(signal.strategy_kind, signal.strategy);
    return QTrading::Contracts::IsCarryLikeStrategy(kind);
}

bool EndsWith(const std::string& value, std::string_view suffix)
{
    return value.size() >= suffix.size() &&
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string PairRootSymbol(const std::string& symbol)
{
    if (EndsWith(symbol, "_SPOT") || EndsWith(symbol, "_PERP")) {
        return symbol.substr(0, symbol.size() - 5);
    }
    return symbol;
}

bool IsSpotOrPerpSymbol(const std::string& symbol)
{
    return EndsWith(symbol, "_SPOT") || EndsWith(symbol, "_PERP");
}

bool IsSpotSymbol(const std::string& symbol)
{
    return EndsWith(symbol, "_SPOT");
}

} // namespace

LiquidityAwareExecutionScheduler::LiquidityAwareExecutionScheduler(Config cfg)
    : cfg_(cfg)
{
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
    const ExecutionSignal& signal,
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
        IsCarryLikeStrategy(signal) &&
        (!cfg_.carry_apply_only_low_urgency ||
         signal.urgency == ExecutionUrgency::Low);
    const bool apply_window_budget =
        cfg_.carry_window_budget_enabled &&
        IsCarryLikeStrategy(signal) &&
        (!cfg_.carry_apply_only_low_urgency ||
         signal.urgency == ExecutionUrgency::Low);
    const bool apply_increase_batching =
        cfg_.carry_increase_batching_enabled &&
        IsCarryLikeStrategy(signal) &&
        (!cfg_.carry_apply_only_low_urgency ||
         signal.urgency == ExecutionUrgency::Low);
    const bool apply_pair_balancing =
        IsCarryLikeStrategy(signal) &&
        (!cfg_.carry_apply_only_low_urgency ||
         signal.urgency == ExecutionUrgency::Low);

    if ((!apply_participation_cap && !apply_window_budget && !apply_increase_batching) ||
        !has_symbol_index_ ||
        market->trade_klines_by_id.empty()) {
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

    struct PendingSlice {
        ExecutionSlice slice{};
        std::string root{};
        double current_notional{ 0.0 };
        double delta_notional{ 0.0 };
        bool reduce_only{ false };
        bool is_pair_leg{ false };
        bool is_spot_leg{ false };
    };

    std::vector<PendingSlice> pending_slices;
    pending_slices.reserve(parent_orders.size());

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
        double current_notional = current_notional_by_symbol[parent.symbol];
        double delta_notional = parent_target_notional - current_notional;
        const auto id_it = symbol_to_id_.find(parent.symbol);
        if (id_it != symbol_to_id_.end()) {
            const double quote_volume = QuoteVolumeFromId(market, id_it->second);
            if (quote_volume > 0.0) {
                const double raw_delta = delta_notional;
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
                delta_notional = clipped_delta;
                slice_target_notional = current_notional + clipped_delta;
            }
        }

        pending_slices.push_back(
            PendingSlice{
                ExecutionSlice{
                    parent.ts_ms,
                    parent.symbol,
                    slice_target_notional,
                    parent.leverage,
                },
                PairRootSymbol(parent.symbol),
                current_notional,
                delta_notional,
                (current_notional != 0.0) && ((current_notional * delta_notional) < 0.0),
                IsSpotOrPerpSymbol(parent.symbol),
                IsSpotSymbol(parent.symbol),
            });
    }

    if (apply_pair_balancing) {
        struct PairState {
            std::size_t spot_index = std::numeric_limits<std::size_t>::max();
            std::size_t perp_index = std::numeric_limits<std::size_t>::max();
            bool spot_non_reduce = false;
            bool perp_non_reduce = false;
        };

        std::unordered_map<std::string, PairState> pair_states;
        pair_states.reserve(pending_slices.size());
        for (std::size_t i = 0; i < pending_slices.size(); ++i) {
            const auto& pending = pending_slices[i];
            if (!pending.is_pair_leg) {
                continue;
            }
            auto& state = pair_states[pending.root];
            if (pending.is_spot_leg) {
                state.spot_index = i;
                state.spot_non_reduce = !pending.reduce_only;
            }
            else {
                state.perp_index = i;
                state.perp_non_reduce = !pending.reduce_only;
            }
        }

        for (const auto& [_, state] : pair_states) {
            if (state.spot_index == std::numeric_limits<std::size_t>::max() ||
                state.perp_index == std::numeric_limits<std::size_t>::max()) {
                continue;
            }
            if (!state.spot_non_reduce || !state.perp_non_reduce) {
                continue;
            }

            auto& spot = pending_slices[state.spot_index];
            auto& perp = pending_slices[state.perp_index];
            const double spot_abs = std::fabs(spot.delta_notional);
            const double perp_abs = std::fabs(perp.delta_notional);
            const bool opposite_direction =
                (spot.delta_notional > 0.0 && perp.delta_notional < 0.0) ||
                (spot.delta_notional < 0.0 && perp.delta_notional > 0.0);

            if (spot_abs <= 0.0 || perp_abs <= 0.0 || !opposite_direction) {
                spot.delta_notional = 0.0;
                spot.slice.target_notional = spot.current_notional;
                perp.delta_notional = 0.0;
                perp.slice.target_notional = perp.current_notional;
                continue;
            }

            const double balanced_abs = std::min(spot_abs, perp_abs);
            if (balanced_abs < cfg_.carry_min_slice_notional_usdt) {
                spot.delta_notional = 0.0;
                spot.slice.target_notional = spot.current_notional;
                perp.delta_notional = 0.0;
                perp.slice.target_notional = perp.current_notional;
                continue;
            }

            spot.delta_notional = std::copysign(balanced_abs, spot.delta_notional);
            spot.slice.target_notional = spot.current_notional + spot.delta_notional;
            perp.delta_notional = std::copysign(balanced_abs, perp.delta_notional);
            perp.slice.target_notional = perp.current_notional + perp.delta_notional;
        }
    }

    for (const auto& pending : pending_slices) {
        slices.push_back(pending.slice);
    }

    return slices;
}

} // namespace QTrading::Execution
