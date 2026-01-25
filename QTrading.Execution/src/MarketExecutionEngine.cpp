#include "Execution/MarketExecutionEngine.hpp"

#include <cmath>
#include <unordered_map>

namespace QTrading::Execution {

MarketExecutionEngine::MarketExecutionEngine(
    std::shared_ptr<QTrading::Infra::Exchanges::IExchange<
        std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>>> exchange,
    Config cfg)
    : exchange_(std::move(exchange)), cfg_(cfg)
{
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

    if (has_symbol_index_ && !market->klines_by_id.empty()) {
        std::vector<double> price_by_id(market->klines_by_id.size(), 0.0);
        for (std::size_t i = 0; i < market->klines_by_id.size(); ++i) {
            const auto& opt = market->klines_by_id[i];
            if (opt.has_value()) {
                price_by_id[i] = opt->ClosePrice;
            }
        }

        std::vector<double> current_notional(price_by_id.size(), 0.0);
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

        for (const auto& kv : target.target_positions) {
            const auto& symbol = kv.first;
            auto it = symbol_to_id_.find(symbol);
            if (it == symbol_to_id_.end()) {
                continue;
            }
            const std::size_t id = it->second;
            if (id >= price_by_id.size() || price_by_id[id] <= 0.0) {
                continue;
            }

            const double target_notional = kv.second;
            const double cur_notional = current_notional[id];
            const double delta_notional = target_notional - cur_notional;
            const double abs_notional = std::fabs(delta_notional);
            if (abs_notional < cfg_.min_notional) {
                continue;
            }

            ExecutionOrder ord;
            ord.ts_ms = market->Timestamp;
            ord.symbol = symbol;
            ord.action = delta_notional > 0.0 ? OrderAction::Buy : OrderAction::Sell;
            ord.qty = abs_notional / price_by_id[id];
            ord.type = OrderType::Market;
            ord.price = 0.0;
            ord.reduce_only = (cur_notional != 0.0) && (cur_notional * delta_notional < 0.0);
            ord.urgency = (signal.urgency == QTrading::Signal::SignalUrgency::High)
                ? OrderUrgency::High
                : (signal.urgency == QTrading::Signal::SignalUrgency::Medium)
                ? OrderUrgency::Medium
                : OrderUrgency::Low;
            orders.push_back(std::move(ord));
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
    for (const auto& pos : exchange_->get_all_positions()) {
        auto it = price_by_symbol.find(pos.symbol);
        if (it == price_by_symbol.end() || it->second <= 0.0) {
            continue;
        }
        const double price = it->second;
        const double sign = pos.is_long ? 1.0 : -1.0;
        current_notional[pos.symbol] += pos.quantity * price * sign;
    }

    for (const auto& kv : target.target_positions) {
        const auto& symbol = kv.first;
        const double target_notional = kv.second;
        const double cur_notional = current_notional[symbol];
        const double delta_notional = target_notional - cur_notional;

        auto pit = price_by_symbol.find(symbol);
        if (pit == price_by_symbol.end() || pit->second <= 0.0) {
            continue;
        }
        const double price = pit->second;
        const double abs_notional = std::fabs(delta_notional);
        if (abs_notional < cfg_.min_notional) {
            continue;
        }

        ExecutionOrder ord;
        ord.ts_ms = market->Timestamp;
        ord.symbol = symbol;
        ord.action = delta_notional > 0.0 ? OrderAction::Buy : OrderAction::Sell;
        ord.qty = abs_notional / price;
        ord.type = OrderType::Market;
        ord.price = 0.0;
        ord.reduce_only = (cur_notional != 0.0) && (cur_notional * delta_notional < 0.0);
        ord.urgency = (signal.urgency == QTrading::Signal::SignalUrgency::High)
            ? OrderUrgency::High
            : (signal.urgency == QTrading::Signal::SignalUrgency::Medium)
            ? OrderUrgency::Medium
            : OrderUrgency::Low;
        orders.push_back(std::move(ord));
    }

    return orders;
}

} // namespace QTrading::Execution
