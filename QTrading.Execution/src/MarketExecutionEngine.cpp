#include "Execution/MarketExecutionEngine.hpp"

#include "Contracts/StrategyIdentity.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>

namespace QTrading::Execution {
namespace {

double ClampPositive(double value, double fallback)
{
    if (!std::isfinite(value) || value <= 0.0) {
        return fallback;
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

double ClampNonNegative(double value)
{
    if (!std::isfinite(value) || value < 0.0) {
        return 0.0;
    }
    return value;
}

double Lerp(double lo, double hi, double t)
{
    return lo + (hi - lo) * Clamp01(t);
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

QTrading::Contracts::StrategyKind ResolveStrategyKind(
    const QTrading::Execution::ExecutionSignal& signal)
{
    return QTrading::Contracts::ResolveStrategyKind(signal.strategy_kind, signal.strategy);
}

bool ShouldSkipDueToExistingOpenOrder(
    const std::unordered_map<std::string, bool>& has_open_order_by_symbol,
    const std::string& symbol,
    QTrading::Contracts::StrategyKind strategy_kind)
{
    // Basis arbitrage should keep converging even if one stale leg order exists.
    // Funding carry keeps the old conservative behavior (one active order per symbol).
    if (strategy_kind == QTrading::Contracts::StrategyKind::BasisArbitrage) {
        return false;
    }
    return has_open_order_by_symbol.find(symbol) != has_open_order_by_symbol.end();
}

bool EndsWith(const std::string& value, std::string_view suffix)
{
    return value.size() >= suffix.size() &&
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string PairRootSymbol(const std::string& symbol)
{
    if (EndsWith(symbol, "_SPOT")) {
        return symbol.substr(0, symbol.size() - 5);
    }
    if (EndsWith(symbol, "_PERP")) {
        return symbol.substr(0, symbol.size() - 5);
    }
    return symbol;
}

}

MarketExecutionEngine::MarketExecutionEngine(
    std::shared_ptr<QTrading::Infra::Exchanges::IExchange<
        std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>>> exchange,
    Config cfg)
    : exchange_(std::move(exchange)), cfg_(cfg)
{
    cfg_.min_notional = ClampPositive(cfg_.min_notional, 5.0);
    carry_min_rebalance_notional_ = ClampPositive(carry_min_rebalance_notional_, cfg_.min_notional);
    cfg_.carry_max_rebalance_step_ratio = Clamp01(cfg_.carry_max_rebalance_step_ratio);
    cfg_.carry_large_notional_step_ratio = Clamp01(cfg_.carry_large_notional_step_ratio);
    cfg_.carry_large_notional_threshold = ClampPositive(cfg_.carry_large_notional_threshold, 50000.0);
    cfg_.carry_max_participation_rate = Clamp01(cfg_.carry_max_participation_rate);
    cfg_.carry_window_budget_participation_rate =
        Clamp01(cfg_.carry_window_budget_participation_rate);
    cfg_.carry_bootstrap_gap_ratio = Clamp01(cfg_.carry_bootstrap_gap_ratio);
    cfg_.carry_bootstrap_step_ratio = Clamp01(cfg_.carry_bootstrap_step_ratio);
    cfg_.carry_bootstrap_participation_rate = Clamp01(cfg_.carry_bootstrap_participation_rate);
    cfg_.carry_min_rebalance_notional_ratio = ClampNonNegative(cfg_.carry_min_rebalance_notional_ratio);
    cfg_.carry_confidence_step_scale_min = ClampNonNegative(cfg_.carry_confidence_step_scale_min);
    cfg_.carry_confidence_step_scale_max = ClampNonNegative(cfg_.carry_confidence_step_scale_max);
    if (cfg_.carry_confidence_step_scale_max < cfg_.carry_confidence_step_scale_min) {
        cfg_.carry_confidence_step_scale_max = cfg_.carry_confidence_step_scale_min;
    }
    cfg_.carry_confidence_participation_scale_min = ClampNonNegative(cfg_.carry_confidence_participation_scale_min);
    cfg_.carry_confidence_participation_scale_max = ClampNonNegative(cfg_.carry_confidence_participation_scale_max);
    if (cfg_.carry_confidence_participation_scale_max < cfg_.carry_confidence_participation_scale_min) {
        cfg_.carry_confidence_participation_scale_max = cfg_.carry_confidence_participation_scale_min;
    }
    cfg_.carry_confidence_cooldown_scale_min = ClampNonNegative(cfg_.carry_confidence_cooldown_scale_min);
    cfg_.carry_confidence_cooldown_scale_max = ClampNonNegative(cfg_.carry_confidence_cooldown_scale_max);
    if (cfg_.carry_confidence_cooldown_scale_max < cfg_.carry_confidence_cooldown_scale_min) {
        cfg_.carry_confidence_cooldown_scale_max = cfg_.carry_confidence_cooldown_scale_min;
    }
    cfg_.carry_maker_limit_offset_bps = ClampNonNegative(cfg_.carry_maker_limit_offset_bps);
    cfg_.carry_maker_catchup_gap_ratio = ClampNonNegative(cfg_.carry_maker_catchup_gap_ratio);
    cfg_.carry_target_anchor_update_ratio = ClampNonNegative(cfg_.carry_target_anchor_update_ratio);
}

std::vector<ExecutionOrder> MarketExecutionEngine::plan(
    const QTrading::Risk::RiskTarget& target,
    const ExecutionSignal& signal,
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    std::vector<ExecutionOrder> orders;
    if (!exchange_ || !market) {
        return orders;
    }
    struct CarryRebalanceOrderMeta {
        std::size_t order_index{ 0 };
        std::string pair_root{};
        double signed_notional{ 0.0 };
        double reference_price{ 0.0 };
    };
    std::vector<CarryRebalanceOrderMeta> carry_rebalance_order_meta;
    carry_rebalance_order_meta.reserve(target.target_positions.size());

    auto apply_two_sided_carry_controls = [&](
        std::vector<ExecutionOrder>& out_orders,
        const std::vector<CarryRebalanceOrderMeta>& meta,
        double min_notional) {
            if (meta.empty()) {
                return;
            }
            if (!cfg_.carry_require_two_sided_rebalance &&
                !cfg_.carry_balance_two_sided_rebalance) {
                return;
            }

            std::unordered_map<std::string, std::vector<const CarryRebalanceOrderMeta*>> meta_by_pair;
            meta_by_pair.reserve(meta.size());
            for (const auto& m : meta) {
                if (m.order_index >= out_orders.size()) {
                    continue;
                }
                meta_by_pair[m.pair_root].push_back(&m);
            }

            std::vector<bool> remove(out_orders.size(), false);
            for (const auto& [pair_root, group] : meta_by_pair) {
                double total_buy_notional = 0.0;
                double total_sell_notional = 0.0;
                for (const auto* m : group) {
                    if (m->signed_notional > 0.0) {
                        total_buy_notional += m->signed_notional;
                    }
                    else if (m->signed_notional < 0.0) {
                        total_sell_notional += -m->signed_notional;
                    }
                }

                if (total_buy_notional <= 0.0 || total_sell_notional <= 0.0) {
                    if (!cfg_.carry_require_two_sided_rebalance) {
                        continue;
                    }
                    for (const auto* m : group) {
                        if (m->order_index < remove.size()) {
                            remove[m->order_index] = true;
                        }
                    }
                    continue;
                }

                if (!cfg_.carry_balance_two_sided_rebalance) {
                    continue;
                }

                const bool scale_buy = total_buy_notional > total_sell_notional;
                const double larger = scale_buy ? total_buy_notional : total_sell_notional;
                const double smaller = scale_buy ? total_sell_notional : total_buy_notional;
                if (larger <= 0.0 || smaller <= 0.0) {
                    continue;
                }
                const double scale = Clamp01(smaller / larger);
                if (scale >= 0.999999) {
                    continue;
                }

                for (const auto* m : group) {
                    const bool is_buy_side = m->signed_notional > 0.0;
                    if ((scale_buy && !is_buy_side) || (!scale_buy && is_buy_side)) {
                        continue;
                    }
                    const double adjusted_notional = std::fabs(m->signed_notional) * scale;
                    if (adjusted_notional < min_notional || m->reference_price <= 0.0) {
                        remove[m->order_index] = true;
                        continue;
                    }
                    out_orders[m->order_index].qty = adjusted_notional / m->reference_price;
                }
            }

            std::vector<ExecutionOrder> filtered;
            filtered.reserve(out_orders.size());
            for (std::size_t i = 0; i < out_orders.size(); ++i) {
                if (!remove[i]) {
                    filtered.push_back(std::move(out_orders[i]));
                }
            }
            out_orders = std::move(filtered);
        };

    const uint64_t day_key = market->Timestamp / 86'400'000ull;

    auto can_emit_carry_rebalance = [&](const std::string& symbol) -> bool {
        if (cfg_.carry_max_rebalances_per_day == 0) {
            return true;
        }

        auto day_it = carry_day_key_by_symbol_.find(symbol);
        if (day_it == carry_day_key_by_symbol_.end() || day_it->second != day_key) {
            carry_day_key_by_symbol_[symbol] = day_key;
            carry_rebalance_count_by_symbol_[symbol] = 0;
            return true;
        }

        const uint32_t count = carry_rebalance_count_by_symbol_[symbol];
        return count < cfg_.carry_max_rebalances_per_day;
    };

    auto mark_carry_rebalance = [&](const std::string& symbol) {
        if (cfg_.carry_max_rebalances_per_day == 0) {
            return;
        }
        auto day_it = carry_day_key_by_symbol_.find(symbol);
        if (day_it == carry_day_key_by_symbol_.end() || day_it->second != day_key) {
            carry_day_key_by_symbol_[symbol] = day_key;
            carry_rebalance_count_by_symbol_[symbol] = 0;
        }
        carry_rebalance_count_by_symbol_[symbol] += 1;
    };

    auto advance_carry_window_budget = [&](const std::string& symbol, double quote_volume) {
        if (!cfg_.carry_window_budget_enabled || cfg_.carry_window_budget_ms == 0) {
            return;
        }
        auto [start_it, inserted] =
            carry_window_start_ts_by_symbol_.emplace(symbol, market->Timestamp);
        if (!inserted) {
            const uint64_t start_ts = start_it->second;
            if (market->Timestamp >= start_ts + cfg_.carry_window_budget_ms) {
                start_it->second = market->Timestamp;
                carry_window_cum_quote_volume_by_symbol_[symbol] = 0.0;
                carry_window_used_notional_by_symbol_[symbol] = 0.0;
            }
        }
        if (quote_volume > 0.0) {
            carry_window_cum_quote_volume_by_symbol_[symbol] += quote_volume;
        }
    };

    auto remaining_carry_window_budget = [&](const std::string& symbol) {
        if (!cfg_.carry_window_budget_enabled || cfg_.carry_window_budget_ms == 0) {
            return std::numeric_limits<double>::infinity();
        }
        const double cum_quote_volume = carry_window_cum_quote_volume_by_symbol_[symbol];
        const double used_notional = carry_window_used_notional_by_symbol_[symbol];
        const double budget_notional = cum_quote_volume * cfg_.carry_window_budget_participation_rate;
        return std::max(0.0, budget_notional - used_notional);
    };

    auto mark_carry_window_budget_use = [&](const std::string& symbol, double abs_notional) {
        if (!cfg_.carry_window_budget_enabled || cfg_.carry_window_budget_ms == 0) {
            return;
        }
        if (abs_notional > 0.0) {
            carry_window_used_notional_by_symbol_[symbol] += abs_notional;
        }
    };

    const auto strategy_kind = ResolveStrategyKind(signal);
    double effective_min_notional = cfg_.min_notional;
    if (QTrading::Contracts::IsCarryLikeStrategy(strategy_kind) &&
        signal.urgency == ExecutionUrgency::Low) {
        // Funding carry is slow by nature; ignore micro re-hedges to reduce churn/fees.
        effective_min_notional = std::max(effective_min_notional, carry_min_rebalance_notional_);
    }
    const double carry_confidence = Clamp01(signal.confidence);
    auto apply_carry_target_anchor = [&](const std::string& symbol,
        double raw_target_notional,
        double min_rebalance_notional) {
            if (!cfg_.carry_target_anchor_enabled) {
                return raw_target_notional;
            }

            auto [it, inserted] =
                carry_target_anchor_notional_by_symbol_.emplace(symbol, raw_target_notional);
            if (inserted) {
                return raw_target_notional;
            }

            const double anchored = it->second;
            const double update_threshold = std::max(
                min_rebalance_notional,
                std::fabs(raw_target_notional) * cfg_.carry_target_anchor_update_ratio);
            const bool cross_zero =
                ((anchored > 0.0 && raw_target_notional < 0.0) ||
                    (anchored < 0.0 && raw_target_notional > 0.0));
            if (cross_zero || std::fabs(raw_target_notional - anchored) >= update_threshold) {
                it->second = raw_target_notional;
                return raw_target_notional;
            }
            return anchored;
        };

    if (!has_symbol_index_ && market->symbols) {
        const auto& symbols = *market->symbols;
        symbol_to_id_.clear();
        symbol_to_id_.reserve(symbols.size() * 2);
        for (std::size_t i = 0; i < symbols.size(); ++i) {
            symbol_to_id_[symbols[i]] = i;
        }
        has_symbol_index_ = !symbols.empty();
    }

    for (const auto& kv : target.leverage) {
        exchange_->set_symbol_leverage(kv.first, kv.second);
    }

    std::unordered_map<std::string, bool> has_open_order_by_symbol;
    has_open_order_by_symbol.reserve(target.target_positions.size() * 2);
    for (const auto& ord : exchange_->get_all_open_orders()) {
        has_open_order_by_symbol[ord.symbol] = true;
    }

    if (has_symbol_index_ && !market->trade_klines_by_id.empty()) {
        std::vector<double> price_by_id(market->trade_klines_by_id.size(), 0.0);
        for (std::size_t i = 0; i < market->trade_klines_by_id.size(); ++i) {
            const auto& opt = market->trade_klines_by_id[i];
            if (opt.has_value()) {
                price_by_id[i] = opt->ClosePrice;
            }
        }

        std::vector<double> current_notional(price_by_id.size(), 0.0);
        std::vector<double> pending_notional(price_by_id.size(), 0.0);
        for (const auto& pos : exchange_->get_all_positions()) {
            auto it = symbol_to_id_.find(pos.symbol);
            if (it == symbol_to_id_.end()) {
                continue;
            }
            const std::size_t id = it->second;
            if (id >= price_by_id.size() || price_by_id[id] <= 0.0) {
                continue;
            }
            const double sign = pos.is_long ? 1.0 : -1.0;
            current_notional[id] += pos.quantity * price_by_id[id] * sign;
        }
        for (const auto& ord : exchange_->get_all_open_orders()) {
            auto it = symbol_to_id_.find(ord.symbol);
            if (it == symbol_to_id_.end()) {
                continue;
            }
            const std::size_t id = it->second;
            if (id >= price_by_id.size() || price_by_id[id] <= 0.0) {
                continue;
            }
            const double sign = (ord.side == QTrading::Dto::Trading::OrderSide::Buy) ? 1.0 : -1.0;
            pending_notional[id] += ord.quantity * price_by_id[id] * sign;
        }

        for (const auto& kv : target.target_positions) {
            const auto& symbol = kv.first;
            if (ShouldSkipDueToExistingOpenOrder(has_open_order_by_symbol, symbol, strategy_kind)) {
                continue;
            }
            auto it = symbol_to_id_.find(symbol);
            if (it == symbol_to_id_.end()) {
                continue;
            }
            const std::size_t id = it->second;
            if (id >= price_by_id.size() || price_by_id[id] <= 0.0) {
                continue;
            }

            const double target_notional = kv.second;
            const double cur_notional = current_notional[id] + pending_notional[id];
            const double raw_delta_notional = target_notional - cur_notional;
            bool reduce_only =
                (cur_notional != 0.0) && (cur_notional * raw_delta_notional < 0.0);
            const bool is_pair_trade_order = QTrading::Contracts::IsCarryLikeStrategy(strategy_kind) &&
                !reduce_only;
            const bool is_carry_rebalance = QTrading::Contracts::IsCarryLikeStrategy(strategy_kind) &&
                (signal.urgency == ExecutionUrgency::Low) &&
                !reduce_only;
            double planned_target_notional = target_notional;
            double delta_notional = raw_delta_notional;
            double symbol_min_notional = effective_min_notional;
            double symbol_step_ratio = cfg_.carry_max_rebalance_step_ratio;
            double symbol_participation_rate = cfg_.carry_max_participation_rate;
            double quote_volume = 0.0;
            double gap_ratio = 0.0;
            if (is_carry_rebalance) {
                const double target_abs_notional = std::fabs(target_notional);
                uint64_t symbol_cooldown_ms = cfg_.carry_rebalance_cooldown_ms;
                symbol_min_notional = std::max(
                    symbol_min_notional,
                    target_abs_notional * cfg_.carry_min_rebalance_notional_ratio);
                planned_target_notional = apply_carry_target_anchor(
                    symbol,
                    target_notional,
                    symbol_min_notional);
                delta_notional = planned_target_notional - cur_notional;
                if (cfg_.carry_confidence_adaptive_enabled) {
                    const double step_scale = Lerp(
                        cfg_.carry_confidence_step_scale_min,
                        cfg_.carry_confidence_step_scale_max,
                        carry_confidence);
                    const double part_scale = Lerp(
                        cfg_.carry_confidence_participation_scale_min,
                        cfg_.carry_confidence_participation_scale_max,
                        carry_confidence);
                    const double cooldown_scale = Lerp(
                        cfg_.carry_confidence_cooldown_scale_max,
                        cfg_.carry_confidence_cooldown_scale_min,
                        carry_confidence);

                    symbol_step_ratio = Clamp01(symbol_step_ratio * step_scale);
                    symbol_participation_rate = Clamp01(symbol_participation_rate * part_scale);
                    if (symbol_cooldown_ms > 0) {
                        const double scaled = static_cast<double>(symbol_cooldown_ms) * cooldown_scale;
                        symbol_cooldown_ms = static_cast<uint64_t>(std::llround(std::max(0.0, scaled)));
                        if (symbol_cooldown_ms == 0) {
                            symbol_cooldown_ms = 1;
                        }
                    }
                }

                const double planned_target_abs_notional = std::fabs(planned_target_notional);
                if (planned_target_abs_notional > 0.0) {
                    gap_ratio = std::fabs(delta_notional) / planned_target_abs_notional;
                    if (gap_ratio >= cfg_.carry_bootstrap_gap_ratio) {
                        symbol_step_ratio = std::max(symbol_step_ratio, cfg_.carry_bootstrap_step_ratio);
                        symbol_participation_rate = std::max(
                            symbol_participation_rate,
                            cfg_.carry_bootstrap_participation_rate);
                        symbol_cooldown_ms = std::min(symbol_cooldown_ms, cfg_.carry_bootstrap_cooldown_ms);
                    }
                }

                if (planned_target_abs_notional >= cfg_.carry_large_notional_threshold) {
                    symbol_step_ratio = std::min(symbol_step_ratio, cfg_.carry_large_notional_step_ratio);
                    symbol_cooldown_ms = std::max(
                        symbol_cooldown_ms,
                        cfg_.carry_large_notional_cooldown_ms);
                }
                const auto last_it = last_carry_order_ts_by_symbol_.find(symbol);
                if (last_it != last_carry_order_ts_by_symbol_.end() &&
                    market->Timestamp < last_it->second + symbol_cooldown_ms)
                {
                    continue;
                }
                if (!can_emit_carry_rebalance(symbol)) {
                    continue;
                }
                if (symbol_step_ratio < 1.0) {
                    const double max_step = std::max(
                        symbol_min_notional,
                        std::fabs(planned_target_notional) * symbol_step_ratio);
                    delta_notional = std::clamp(delta_notional, -max_step, max_step);
                }
                quote_volume = QuoteVolumeFromId(market, id);
                if (cfg_.carry_window_budget_enabled) {
                    advance_carry_window_budget(symbol, quote_volume);
                    const double remaining_budget = remaining_carry_window_budget(symbol);
                    delta_notional = std::clamp(delta_notional, -remaining_budget, remaining_budget);
                }
                if (symbol_participation_rate > 0.0) {
                    if (quote_volume > 0.0) {
                        const double volume_cap = quote_volume * symbol_participation_rate;
                        delta_notional = std::clamp(delta_notional, -volume_cap, volume_cap);
                    }
                }
            }

            const double abs_notional = std::fabs(delta_notional);
            if (abs_notional < symbol_min_notional) {
                continue;
            }

            ExecutionOrder ord;
            ord.ts_ms = market->Timestamp;
            ord.symbol = symbol;
            ord.action = delta_notional > 0.0 ? OrderAction::Buy : OrderAction::Sell;
            ord.qty = abs_notional / price_by_id[id];
            bool use_maker_limit =
                cfg_.carry_maker_first_enabled &&
                is_carry_rebalance &&
                (cfg_.carry_maker_limit_offset_bps > 0.0) &&
                (gap_ratio < cfg_.carry_maker_catchup_gap_ratio);
            ord.type = use_maker_limit ? OrderType::Limit : OrderType::Market;
            if (use_maker_limit) {
                const double bps = cfg_.carry_maker_limit_offset_bps * 1e-4;
                const double price_scale =
                    (ord.action == OrderAction::Buy) ? (1.0 - bps) : (1.0 + bps);
                ord.price = std::max(0.0, price_by_id[id] * price_scale);
            }
            else {
                ord.price = 0.0;
            }
            ord.reduce_only = reduce_only;
            ord.urgency = (signal.urgency == ExecutionUrgency::High)
                ? OrderUrgency::High
                : (signal.urgency == ExecutionUrgency::Medium)
                ? OrderUrgency::Medium
                : OrderUrgency::Low;
            orders.push_back(std::move(ord));
            if (is_pair_trade_order) {
                carry_rebalance_order_meta.push_back(
                    { orders.size() - 1, PairRootSymbol(symbol), delta_notional, price_by_id[id] });
            }
            if (is_carry_rebalance) {
                last_carry_order_ts_by_symbol_[symbol] = market->Timestamp;
                mark_carry_rebalance(symbol);
                mark_carry_window_budget_use(symbol, abs_notional);
            }
        }
        apply_two_sided_carry_controls(orders, carry_rebalance_order_meta, effective_min_notional);
        return orders;
    }

    return orders;
}

} // namespace QTrading::Execution

