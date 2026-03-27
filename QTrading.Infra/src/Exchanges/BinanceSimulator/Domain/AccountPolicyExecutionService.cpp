#include "Exchanges/BinanceSimulator/Domain/AccountPolicyExecutionService.hpp"

#include <cmath>
#include <utility>

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

namespace {

std::pair<bool, bool> default_can_fill_and_taker(
    const QTrading::dto::Order& order,
    const QTrading::Dto::Market::Binance::TradeKlineDto& kline)
{
    if (order.price <= 0.0) {
        return { true, true };
    }

    if (order.side == QTrading::Dto::Trading::OrderSide::Buy) {
        if (kline.LowPrice > order.price) {
            return { false, false };
        }
        return { true, kline.ClosePrice <= order.price };
    }

    if (kline.HighPrice < order.price) {
        return { false, false };
    }
    return { true, kline.ClosePrice >= order.price };
}

double default_execution_price(
    const QTrading::dto::Order& order,
    const QTrading::Dto::Market::Binance::TradeKlineDto& kline)
{
    if (order.price <= 0.0) {
        return kline.ClosePrice;
    }
    return order.price;
}

} // namespace

bool AccountPolicyExecutionService::TryQueueOrder(
    const std::string& symbol,
    double quantity,
    double price,
    QTrading::Dto::Trading::OrderSide side,
    QTrading::Dto::Trading::PositionSide position_side,
    bool reduce_only,
    int& next_order_id,
    std::vector<QTrading::dto::Order>& open_orders)
{
    if (!(quantity > 0.0) || !std::isfinite(quantity)) {
        return false;
    }

    QTrading::dto::Order order{};
    order.id = next_order_id++;
    order.symbol = symbol;
    order.quantity = quantity;
    order.price = price;
    order.side = side;
    order.position_side = position_side;
    order.reduce_only = reduce_only;
    order.instrument_type = QTrading::Dto::Trading::InstrumentType::Perp;
    open_orders.push_back(std::move(order));
    return true;
}

AccountPolicyUpdateResult AccountPolicyExecutionService::ApplyUpdates(
    const std::unordered_map<std::string, QTrading::Dto::Market::Binance::TradeKlineDto>& symbol_kline,
    const std::unordered_map<std::string, double>& symbol_mark_price,
    const AccountPolicies& policies,
    int vip_level,
    const std::unordered_map<std::string, double>& symbol_leverage,
    std::vector<QTrading::dto::Order>& open_orders,
    std::vector<QTrading::dto::Position>& positions)
{
    AccountPolicyUpdateResult result{};
    if (open_orders.empty()) {
        return result;
    }

    std::vector<QTrading::dto::Order> remaining_orders{};
    remaining_orders.reserve(open_orders.size());

    for (const auto& order : open_orders) {
        const auto kline_it = symbol_kline.find(order.symbol);
        if (kline_it == symbol_kline.end()) {
            remaining_orders.push_back(order);
            continue;
        }

        AccountPerSymbolMarketContext market_context{};
        market_context.trade_kline = &kline_it->second;
        const auto mark_it = symbol_mark_price.find(order.symbol);
        if (mark_it != symbol_mark_price.end()) {
            market_context.last_mark_price = mark_it->second;
        }

        std::pair<bool, bool> can_fill_and_taker = default_can_fill_and_taker(order, kline_it->second);
        if (policies.can_fill_and_taker_ctx) {
            can_fill_and_taker = policies.can_fill_and_taker_ctx(order, market_context);
        }

        if (!can_fill_and_taker.first) {
            remaining_orders.push_back(order);
            continue;
        }

        double fill_price = default_execution_price(order, kline_it->second);
        if (policies.execution_price_ctx) {
            fill_price = policies.execution_price_ctx(order, market_context, 0.0, 0.0);
        }

        auto fee_rates = std::make_tuple(0.0, 0.0);
        if (policies.fee_rates) {
            fee_rates = policies.fee_rates(vip_level);
        }
        const double maker_fee_rate = std::get<0>(fee_rates);
        const double taker_fee_rate = std::get<1>(fee_rates);
        const double fee_rate = can_fill_and_taker.second ? taker_fee_rate : maker_fee_rate;

        const double notional = order.quantity * fill_price;
        const double fee = notional * fee_rate;
        result.perp_wallet_delta -= fee;

        const auto leverage_it = symbol_leverage.find(order.symbol);
        const double leverage = leverage_it == symbol_leverage.end() ? 1.0 : leverage_it->second;

        QTrading::dto::Position position{};
        position.id = static_cast<int>(positions.size() + 1);
        position.order_id = order.id;
        position.symbol = order.symbol;
        position.quantity = order.quantity;
        position.entry_price = fill_price;
        position.is_long = order.side == QTrading::Dto::Trading::OrderSide::Buy;
        position.unrealized_pnl = 0.0;
        position.notional = notional;
        position.initial_margin = leverage > 0.0 ? (notional / leverage) : notional;
        position.maintenance_margin = 0.0;
        position.fee = fee;
        position.leverage = leverage;
        position.fee_rate = fee_rate;
        position.instrument_type = QTrading::Dto::Trading::InstrumentType::Perp;
        positions.push_back(std::move(position));
        ++result.filled_count;
    }

    open_orders.swap(remaining_orders);
    return result;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain

