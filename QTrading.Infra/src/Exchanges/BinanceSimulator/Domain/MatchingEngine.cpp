#include "Exchanges/BinanceSimulator/Domain/MatchingEngine.hpp"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <limits>

#include "Dto/Market/Binance/MultiKline.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"
#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {
namespace {

constexpr double kEpsilon = 1e-12;

void seed_perp_reducible_quantities(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state,
    std::vector<double>& long_qty,
    std::vector<double>& short_qty)
{
    for (const auto& position : runtime_state.positions) {
        if (position.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp) {
            continue;
        }
        const auto symbol_it = step_state.symbol_to_id.find(position.symbol);
        if (symbol_it == step_state.symbol_to_id.end()) {
            continue;
        }
        const size_t idx = symbol_it->second;
        if (idx == std::numeric_limits<size_t>::max() || idx >= long_qty.size() || idx >= short_qty.size()) {
            continue;
        }
        if (position.is_long) {
            long_qty[idx] += position.quantity;
        }
        else {
            short_qty[idx] += position.quantity;
        }
    }
}

bool is_marketable(const QTrading::dto::Order& order, const QTrading::Dto::Market::Binance::TradeKlineDto& kline)
{
    if (order.price <= 0.0) {
        return true;
    }
    if (order.side == QTrading::Dto::Trading::OrderSide::Buy) {
        return kline.LowPrice <= order.price + kEpsilon;
    }
    return kline.HighPrice + kEpsilon >= order.price;
}

double compute_fill_price(const QTrading::dto::Order& order, const QTrading::Dto::Market::Binance::TradeKlineDto& kline)
{
    if (order.price <= 0.0) {
        return kline.ClosePrice;
    }
    if (order.side == QTrading::Dto::Trading::OrderSide::Buy) {
        return std::min(order.price, kline.ClosePrice);
    }
    return std::max(order.price, kline.ClosePrice);
}

} // namespace

void MatchingEngine::RunStep(
    State::BinanceExchangeRuntimeState& runtime_state,
    State::StepKernelState& step_state,
    const QTrading::Dto::Market::Binance::MultiKlineDto& market,
    std::vector<MatchFill>& out_fills)
{
    out_fills.clear();
    if (!market.symbols || runtime_state.orders.empty()) {
        return;
    }

    if (out_fills.capacity() < runtime_state.orders.size()) {
        out_fills.reserve(runtime_state.orders.size());
    }

    auto& liquidity_left = step_state.matching_liquidity_scratch;
    auto& has_liquidity = step_state.matching_has_liquidity_scratch;
    auto& reducible_long_qty = step_state.matching_reducible_long_scratch;
    auto& reducible_short_qty = step_state.matching_reducible_short_scratch;
    liquidity_left.assign(market.symbols->size(), 0.0);
    has_liquidity.assign(market.symbols->size(), 0);
    reducible_long_qty.assign(market.symbols->size(), 0.0);
    reducible_short_qty.assign(market.symbols->size(), 0.0);
    seed_perp_reducible_quantities(runtime_state, step_state, reducible_long_qty, reducible_short_qty);
    for (size_t i = 0; i < market.symbols->size(); ++i) {
        if (!market.trade_klines_by_id[i].has_value()) {
            continue;
        }
        const double raw = market.trade_klines_by_id[i]->Volume;
        liquidity_left[i] = raw > 0.0 ? raw : std::numeric_limits<double>::infinity();
        has_liquidity[i] = 1;
    }

    auto& orders = runtime_state.orders;
    auto& next_orders = step_state.matching_orders_next_scratch;
    next_orders.clear();
    next_orders.reserve(orders.size());
    for (auto& order_slot : orders) {
        QTrading::dto::Order order = std::move(order_slot);
        const auto symbol_it = step_state.symbol_to_id.find(order.symbol);
        if (symbol_it == step_state.symbol_to_id.end()) {
            next_orders.emplace_back(std::move(order));
            continue;
        }
        const size_t symbol_index = symbol_it->second;
        if (symbol_index == std::numeric_limits<size_t>::max() ||
            symbol_index >= market.trade_klines_by_id.size() ||
            !market.trade_klines_by_id[symbol_index].has_value()) {
            next_orders.emplace_back(std::move(order));
            continue;
        }

        if (!has_liquidity[symbol_index] || liquidity_left[symbol_index] <= kEpsilon) {
            next_orders.emplace_back(std::move(order));
            continue;
        }

        const auto& kline = *market.trade_klines_by_id[symbol_index];
        if (!is_marketable(order, kline)) {
            next_orders.emplace_back(std::move(order));
            continue;
        }

        const double fill_price = compute_fill_price(order, kline);
        const bool is_taker = order.price <= 0.0 ||
            (order.side == QTrading::Dto::Trading::OrderSide::Buy
                ? kline.ClosePrice <= order.price + kEpsilon
                : kline.ClosePrice + kEpsilon >= order.price);
        const double request_qty = std::max(0.0, order.quantity);
        double max_fill_qty = std::min(request_qty, liquidity_left[symbol_index]);
        if (order.instrument_type == QTrading::Dto::Trading::InstrumentType::Perp && order.reduce_only) {
            double reducible_qty = 0.0;
            if (order.side == QTrading::Dto::Trading::OrderSide::Sell) {
                reducible_qty = reducible_long_qty[symbol_index];
            }
            else {
                reducible_qty = reducible_short_qty[symbol_index];
            }
            if (reducible_qty <= kEpsilon) {
                next_orders.emplace_back(std::move(order));
                continue;
            }
            max_fill_qty = std::min(max_fill_qty, reducible_qty);
        }
        const double fill_qty = max_fill_qty;
        if (fill_qty <= kEpsilon) {
            next_orders.emplace_back(std::move(order));
            continue;
        }

        MatchFill fill{};
        fill.order_id = order.id;
        fill.symbol = order.symbol;
        fill.instrument_type = order.instrument_type;
        fill.side = order.side;
        fill.position_side = order.position_side;
        fill.reduce_only = order.reduce_only;
        fill.close_position = order.close_position;
        fill.is_taker = is_taker;
        fill.quantity = fill_qty;
        fill.price = fill_price;
        out_fills.emplace_back(std::move(fill));

        liquidity_left[symbol_index] -= fill_qty;
        if (order.instrument_type == QTrading::Dto::Trading::InstrumentType::Perp && order.reduce_only) {
            if (order.side == QTrading::Dto::Trading::OrderSide::Sell) {
                reducible_long_qty[symbol_index] = std::max(0.0, reducible_long_qty[symbol_index] - fill_qty);
            }
            else {
                reducible_short_qty[symbol_index] = std::max(0.0, reducible_short_qty[symbol_index] - fill_qty);
            }
        }
        order.quantity = request_qty - fill_qty;
        if (order.quantity > kEpsilon) {
            next_orders.emplace_back(std::move(order));
        }
    }
    orders.swap(next_orders);
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
