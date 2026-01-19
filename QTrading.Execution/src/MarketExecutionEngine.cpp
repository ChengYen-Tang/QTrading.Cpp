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

    for (const auto& kv : target.leverage) {
        exchange_->set_symbol_leverage(kv.first, kv.second);
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
