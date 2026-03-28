#include "Exchanges/BinanceSimulator/Domain/MatchingEngine.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>
#include <numeric>

#include "Dto/Market/Binance/MultiKline.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"
#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {
namespace {

constexpr double kEpsilon = 1e-12;

double clamp01(double value) noexcept
{
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

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

bool is_marketable_at_open(const QTrading::dto::Order& order, const QTrading::Dto::Market::Binance::TradeKlineDto& kline)
{
    if (order.price <= 0.0) {
        return true;
    }
    if (order.side == QTrading::Dto::Trading::OrderSide::Buy) {
        return kline.OpenPrice <= order.price + kEpsilon;
    }
    return kline.OpenPrice + kEpsilon >= order.price;
}

double compute_fill_price(const QTrading::dto::Order& order, const QTrading::Dto::Market::Binance::TradeKlineDto& kline)
{
    if (order.price <= 0.0) {
        return kline.ClosePrice;
    }
    return order.price;
}

double infer_taker_buy_ratio(const QTrading::Dto::Market::Binance::TradeKlineDto& kline) noexcept
{
    if (kline.Volume > kEpsilon &&
        kline.TakerBuyBaseVolume >= 0.0 &&
        kline.TakerBuyBaseVolume <= kline.Volume + kEpsilon) {
        return clamp01(kline.TakerBuyBaseVolume / kline.Volume);
    }
    const double range = kline.HighPrice - kline.LowPrice;
    if (range <= kEpsilon) {
        return 0.5;
    }
    return clamp01((kline.ClosePrice - kline.LowPrice) / range);
}

double deterministic_unit_random(uint64_t seed, uint64_t ts, size_t symbol_index, uint32_t sample_index) noexcept
{
    uint64_t x = seed;
    x ^= (ts + 0x9e3779b97f4a7c15ull + (x << 6) + (x >> 2));
    x ^= (static_cast<uint64_t>(symbol_index) + 0x9e3779b97f4a7c15ull + (x << 6) + (x >> 2));
    x ^= (static_cast<uint64_t>(sample_index) + 0x9e3779b97f4a7c15ull + (x << 6) + (x >> 2));
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    const uint64_t r = x * 2685821657736338717ull;
    return static_cast<double>(r >> 11) * (1.0 / 9007199254740992.0);
}

double resolve_taker_buy_ratio(
    const QTrading::Dto::Market::Binance::TradeKlineDto& kline,
    const Config::SimulationConfig& config,
    uint64_t ts_exchange,
    size_t symbol_index) noexcept
{
    const double base_ratio = infer_taker_buy_ratio(kline);
    if (config.intra_bar_path_mode != Config::IntraBarPathMode::MonteCarloPath) {
        return base_ratio;
    }

    const uint32_t samples = std::max<uint32_t>(1u, config.intra_bar_monte_carlo_samples);
    uint32_t buy_hits = 0;
    for (uint32_t i = 0; i < samples; ++i) {
        if (deterministic_unit_random(config.intra_bar_random_seed, ts_exchange, symbol_index, i) < base_ratio) {
            ++buy_hits;
        }
    }
    return static_cast<double>(buy_hits) / static_cast<double>(samples);
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
    auto& buy_liquidity_left = step_state.matching_buy_liquidity_scratch;
    auto& sell_liquidity_left = step_state.matching_sell_liquidity_scratch;
    auto& has_liquidity = step_state.matching_has_liquidity_scratch;
    auto& reducible_long_qty = step_state.matching_reducible_long_scratch;
    auto& reducible_short_qty = step_state.matching_reducible_short_scratch;
    const bool opposite_passive_split =
        runtime_state.simulation_config.kline_volume_split_mode ==
        Config::KlineVolumeSplitMode::OppositePassiveSplit;
    const auto path_mode = runtime_state.simulation_config.intra_bar_path_mode;
    const bool open_marketability_path =
        path_mode == Config::IntraBarPathMode::OpenMarketability ||
        path_mode == Config::IntraBarPathMode::MonteCarloPath;
    liquidity_left.assign(market.symbols->size(), 0.0);
    buy_liquidity_left.assign(market.symbols->size(), 0.0);
    sell_liquidity_left.assign(market.symbols->size(), 0.0);
    has_liquidity.assign(market.symbols->size(), 0);
    reducible_long_qty.assign(market.symbols->size(), 0.0);
    reducible_short_qty.assign(market.symbols->size(), 0.0);
    seed_perp_reducible_quantities(runtime_state, step_state, reducible_long_qty, reducible_short_qty);
    for (size_t i = 0; i < market.symbols->size(); ++i) {
        if (!market.trade_klines_by_id[i].has_value()) {
            continue;
        }
        const double raw = market.trade_klines_by_id[i]->Volume;
        const double base_liquidity = raw > 0.0 ? raw : std::numeric_limits<double>::infinity();
        liquidity_left[i] = base_liquidity;
        if (opposite_passive_split && std::isfinite(base_liquidity)) {
            const double taker_buy_ratio = resolve_taker_buy_ratio(
                *market.trade_klines_by_id[i],
                runtime_state.simulation_config,
                market.Timestamp,
                i);
            buy_liquidity_left[i] = base_liquidity * taker_buy_ratio;
            sell_liquidity_left[i] = base_liquidity - buy_liquidity_left[i];
        }
        else {
            buy_liquidity_left[i] = base_liquidity;
            sell_liquidity_left[i] = base_liquidity;
        }
        has_liquidity[i] = 1;
    }

    auto& orders = runtime_state.orders;
    auto& next_orders = step_state.matching_orders_next_scratch;
    next_orders.clear();
    next_orders.reserve(orders.size());
    auto& order_index = step_state.matching_order_index_scratch;
    order_index.resize(orders.size());
    std::iota(order_index.begin(), order_index.end(), size_t{ 0 });
    std::stable_sort(order_index.begin(), order_index.end(),
        [&](size_t lhs, size_t rhs) {
            const auto& a = orders[lhs];
            const auto& b = orders[rhs];
            if (a.symbol != b.symbol || a.side != b.side) {
                return lhs < rhs;
            }
            if (a.price <= 0.0 || b.price <= 0.0) {
                return lhs < rhs;
            }
            if (a.side == QTrading::Dto::Trading::OrderSide::Buy) {
                if (std::abs(a.price - b.price) > kEpsilon) {
                    return a.price > b.price;
                }
            }
            else {
                if (std::abs(a.price - b.price) > kEpsilon) {
                    return a.price < b.price;
                }
            }
            return a.id < b.id;
        });
    for (const size_t idx : order_index) {
        auto& order_slot = orders[idx];
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
            (open_marketability_path
                ? is_marketable_at_open(order, kline)
                : (order.side == QTrading::Dto::Trading::OrderSide::Buy
                    ? kline.ClosePrice <= order.price + kEpsilon
                    : kline.ClosePrice + kEpsilon >= order.price));
        const double request_qty = std::max(0.0, order.quantity);
        double available_liquidity = liquidity_left[symbol_index];
        if (opposite_passive_split) {
            if (order.side == QTrading::Dto::Trading::OrderSide::Buy) {
                available_liquidity = sell_liquidity_left[symbol_index];
            }
            else {
                available_liquidity = buy_liquidity_left[symbol_index];
            }
        }
        double max_fill_qty = std::min(request_qty, available_liquidity);
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
        if (opposite_passive_split) {
            if (order.side == QTrading::Dto::Trading::OrderSide::Buy) {
                sell_liquidity_left[symbol_index] = std::max(0.0, sell_liquidity_left[symbol_index] - fill_qty);
            }
            else {
                buy_liquidity_left[symbol_index] = std::max(0.0, buy_liquidity_left[symbol_index] - fill_qty);
            }
        }
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
