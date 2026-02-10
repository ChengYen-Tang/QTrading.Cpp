#include "Execution/MarketExecutionEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
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

}

MarketExecutionEngine::MarketExecutionEngine(
    std::shared_ptr<QTrading::Infra::Exchanges::IExchange<
        std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>>> exchange,
    Config cfg)
    : exchange_(std::move(exchange)), cfg_(cfg)
{
    // Optional env overrides for carry sweeps.
    OverrideDoubleFromEnv("QTR_FC_MIN_NOTIONAL", cfg_.min_notional);
    OverrideDoubleFromEnv("QTR_FC_CARRY_MIN_REBALANCE_NOTIONAL", carry_min_rebalance_notional_);
    OverrideUint64FromEnv("QTR_FC_CARRY_REBALANCE_COOLDOWN_MS", cfg_.carry_rebalance_cooldown_ms);
    OverrideDoubleFromEnv("QTR_FC_CARRY_MAX_REBALANCE_STEP_RATIO", cfg_.carry_max_rebalance_step_ratio);
    OverrideDoubleFromEnv("QTR_FC_CARRY_LARGE_NOTIONAL_STEP_RATIO", cfg_.carry_large_notional_step_ratio);
    OverrideDoubleFromEnv("QTR_FC_CARRY_LARGE_NOTIONAL_THRESHOLD", cfg_.carry_large_notional_threshold);
    OverrideUint64FromEnv("QTR_FC_CARRY_LARGE_NOTIONAL_COOLDOWN_MS", cfg_.carry_large_notional_cooldown_ms);
    OverrideDoubleFromEnv("QTR_FC_CARRY_MAX_PARTICIPATION_RATE", cfg_.carry_max_participation_rate);
    OverrideDoubleFromEnv("QTR_FC_CARRY_BOOTSTRAP_GAP_RATIO", cfg_.carry_bootstrap_gap_ratio);
    OverrideDoubleFromEnv("QTR_FC_CARRY_BOOTSTRAP_STEP_RATIO", cfg_.carry_bootstrap_step_ratio);
    OverrideDoubleFromEnv("QTR_FC_CARRY_BOOTSTRAP_PARTICIPATION_RATE", cfg_.carry_bootstrap_participation_rate);
    OverrideUint64FromEnv("QTR_FC_CARRY_BOOTSTRAP_COOLDOWN_MS", cfg_.carry_bootstrap_cooldown_ms);
    OverrideDoubleFromEnv("QTR_FC_CARRY_MIN_REBALANCE_NOTIONAL_RATIO", cfg_.carry_min_rebalance_notional_ratio);
    {
        uint64_t per_day = static_cast<uint64_t>(cfg_.carry_max_rebalances_per_day);
        OverrideUint64FromEnv("QTR_FC_CARRY_MAX_REBALANCES_PER_DAY", per_day);
        cfg_.carry_max_rebalances_per_day = static_cast<uint32_t>(per_day);
    }
    cfg_.min_notional = ClampPositive(cfg_.min_notional, 5.0);
    carry_min_rebalance_notional_ = ClampPositive(carry_min_rebalance_notional_, cfg_.min_notional);
    cfg_.carry_max_rebalance_step_ratio = Clamp01(cfg_.carry_max_rebalance_step_ratio);
    cfg_.carry_large_notional_step_ratio = Clamp01(cfg_.carry_large_notional_step_ratio);
    cfg_.carry_large_notional_threshold = ClampPositive(cfg_.carry_large_notional_threshold, 50000.0);
    cfg_.carry_max_participation_rate = Clamp01(cfg_.carry_max_participation_rate);
    cfg_.carry_bootstrap_gap_ratio = Clamp01(cfg_.carry_bootstrap_gap_ratio);
    cfg_.carry_bootstrap_step_ratio = Clamp01(cfg_.carry_bootstrap_step_ratio);
    cfg_.carry_bootstrap_participation_rate = Clamp01(cfg_.carry_bootstrap_participation_rate);
    cfg_.carry_min_rebalance_notional_ratio = ClampNonNegative(cfg_.carry_min_rebalance_notional_ratio);
}

std::vector<ExecutionOrder> MarketExecutionEngine::plan(
    const QTrading::Risk::RiskTarget& target,
    const QTrading::Signal::SignalDecision& signal,
    const std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>& market)
{
    std::vector<ExecutionOrder> orders;
    if (!exchange_ || !market) {
        return orders;
    }
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

    double effective_min_notional = cfg_.min_notional;
    if (signal.strategy == "funding_carry" && signal.urgency == QTrading::Signal::SignalUrgency::Low) {
        // Funding carry is slow by nature; ignore micro re-hedges to reduce churn/fees.
        effective_min_notional = std::max(effective_min_notional, carry_min_rebalance_notional_);
    }

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

    if (has_symbol_index_ && !market->klines_by_id.empty()) {
        std::vector<double> price_by_id(market->klines_by_id.size(), 0.0);
        for (std::size_t i = 0; i < market->klines_by_id.size(); ++i) {
            const auto& opt = market->klines_by_id[i];
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
            if (has_open_order_by_symbol.find(symbol) != has_open_order_by_symbol.end()) {
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
            double delta_notional = target_notional - cur_notional;
            bool reduce_only = (cur_notional != 0.0) && (cur_notional * delta_notional < 0.0);
            const bool is_carry_rebalance = (signal.strategy == "funding_carry") &&
                (signal.urgency == QTrading::Signal::SignalUrgency::Low) &&
                !reduce_only;
            double symbol_min_notional = effective_min_notional;
            double symbol_step_ratio = cfg_.carry_max_rebalance_step_ratio;
            double symbol_participation_rate = cfg_.carry_max_participation_rate;
            if (is_carry_rebalance) {
                const double target_abs_notional = std::fabs(target_notional);
                uint64_t symbol_cooldown_ms = cfg_.carry_rebalance_cooldown_ms;
                symbol_min_notional = std::max(
                    symbol_min_notional,
                    target_abs_notional * cfg_.carry_min_rebalance_notional_ratio);

                if (target_abs_notional > 0.0) {
                    const double gap_ratio = std::fabs(delta_notional) / target_abs_notional;
                    if (gap_ratio >= cfg_.carry_bootstrap_gap_ratio) {
                        symbol_step_ratio = std::max(symbol_step_ratio, cfg_.carry_bootstrap_step_ratio);
                        symbol_participation_rate = std::max(
                            symbol_participation_rate,
                            cfg_.carry_bootstrap_participation_rate);
                        symbol_cooldown_ms = std::min(symbol_cooldown_ms, cfg_.carry_bootstrap_cooldown_ms);
                    }
                }

                if (target_abs_notional >= cfg_.carry_large_notional_threshold) {
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
                        std::fabs(target_notional) * symbol_step_ratio);
                    delta_notional = std::clamp(delta_notional, -max_step, max_step);
                }
                if (symbol_participation_rate > 0.0) {
                    const double quote_volume = QuoteVolumeFromId(market, id);
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
            ord.type = OrderType::Market;
            ord.price = 0.0;
            ord.reduce_only = reduce_only;
            ord.urgency = (signal.urgency == QTrading::Signal::SignalUrgency::High)
                ? OrderUrgency::High
                : (signal.urgency == QTrading::Signal::SignalUrgency::Medium)
                ? OrderUrgency::Medium
                : OrderUrgency::Low;
            orders.push_back(std::move(ord));
            if (is_carry_rebalance) {
                last_carry_order_ts_by_symbol_[symbol] = market->Timestamp;
                mark_carry_rebalance(symbol);
            }
        }
        return orders;
    }

    std::unordered_map<std::string, double> price_by_symbol;
    price_by_symbol.reserve(market->klines.size());
    for (const auto& kv : market->klines) {
        if (kv.second.has_value()) {
            price_by_symbol[kv.first] = kv.second->ClosePrice;
        }
    }

    std::unordered_map<std::string, double> current_notional;
    std::unordered_map<std::string, double> pending_notional;
    for (const auto& pos : exchange_->get_all_positions()) {
        auto it = price_by_symbol.find(pos.symbol);
        if (it == price_by_symbol.end() || it->second <= 0.0) {
            continue;
        }
        const double price = it->second;
        const double sign = pos.is_long ? 1.0 : -1.0;
        current_notional[pos.symbol] += pos.quantity * price * sign;
    }
    for (const auto& ord : exchange_->get_all_open_orders()) {
        auto it = price_by_symbol.find(ord.symbol);
        if (it == price_by_symbol.end() || it->second <= 0.0) {
            continue;
        }
        const double sign = (ord.side == QTrading::Dto::Trading::OrderSide::Buy) ? 1.0 : -1.0;
        pending_notional[ord.symbol] += ord.quantity * it->second * sign;
    }

    for (const auto& kv : target.target_positions) {
        const auto& symbol = kv.first;
        if (has_open_order_by_symbol.find(symbol) != has_open_order_by_symbol.end()) {
            continue;
        }
        const double target_notional = kv.second;
        const double cur_notional = current_notional[symbol] + pending_notional[symbol];
        double delta_notional = target_notional - cur_notional;
        bool reduce_only = (cur_notional != 0.0) && (cur_notional * delta_notional < 0.0);
        const bool is_carry_rebalance = (signal.strategy == "funding_carry") &&
            (signal.urgency == QTrading::Signal::SignalUrgency::Low) &&
            !reduce_only;
        double symbol_min_notional = effective_min_notional;
        double symbol_step_ratio = cfg_.carry_max_rebalance_step_ratio;
        double symbol_participation_rate = cfg_.carry_max_participation_rate;
        if (is_carry_rebalance) {
            const double target_abs_notional = std::fabs(target_notional);
            uint64_t symbol_cooldown_ms = cfg_.carry_rebalance_cooldown_ms;
            symbol_min_notional = std::max(
                symbol_min_notional,
                target_abs_notional * cfg_.carry_min_rebalance_notional_ratio);

            if (target_abs_notional > 0.0) {
                const double gap_ratio = std::fabs(delta_notional) / target_abs_notional;
                if (gap_ratio >= cfg_.carry_bootstrap_gap_ratio) {
                    symbol_step_ratio = std::max(symbol_step_ratio, cfg_.carry_bootstrap_step_ratio);
                    symbol_participation_rate = std::max(
                        symbol_participation_rate,
                        cfg_.carry_bootstrap_participation_rate);
                    symbol_cooldown_ms = std::min(symbol_cooldown_ms, cfg_.carry_bootstrap_cooldown_ms);
                }
            }

            if (target_abs_notional >= cfg_.carry_large_notional_threshold) {
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
                    std::fabs(target_notional) * symbol_step_ratio);
                delta_notional = std::clamp(delta_notional, -max_step, max_step);
            }
        }

        auto pit = price_by_symbol.find(symbol);
        if (pit == price_by_symbol.end() || pit->second <= 0.0) {
            continue;
        }
        if (is_carry_rebalance && symbol_participation_rate > 0.0) {
            auto kit = market->klines.find(symbol);
            if (kit != market->klines.end() && kit->second.has_value()) {
                const double quote_volume = std::max(0.0, kit->second->QuoteVolume);
                if (quote_volume > 0.0) {
                    const double volume_cap = quote_volume * symbol_participation_rate;
                    delta_notional = std::clamp(delta_notional, -volume_cap, volume_cap);
                }
            }
        }
        const double price = pit->second;
        const double abs_notional = std::fabs(delta_notional);
        if (abs_notional < symbol_min_notional) {
            continue;
        }

        ExecutionOrder ord;
        ord.ts_ms = market->Timestamp;
        ord.symbol = symbol;
        ord.action = delta_notional > 0.0 ? OrderAction::Buy : OrderAction::Sell;
        ord.qty = abs_notional / price;
        ord.type = OrderType::Market;
        ord.price = 0.0;
        ord.reduce_only = reduce_only;
        ord.urgency = (signal.urgency == QTrading::Signal::SignalUrgency::High)
            ? OrderUrgency::High
            : (signal.urgency == QTrading::Signal::SignalUrgency::Medium)
            ? OrderUrgency::Medium
            : OrderUrgency::Low;
        orders.push_back(std::move(ord));
        if (is_carry_rebalance) {
            last_carry_order_ts_by_symbol_[symbol] = market->Timestamp;
            mark_carry_rebalance(symbol);
        }
    }

    return orders;
}

} // namespace QTrading::Execution
