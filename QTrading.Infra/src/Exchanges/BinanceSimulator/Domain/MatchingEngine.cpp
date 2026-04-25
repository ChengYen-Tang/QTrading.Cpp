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

double sigmoid(double x) noexcept
{
    if (x >= 0.0) {
        const double z = std::exp(-x);
        return 1.0 / (1.0 + z);
    }
    const double z = std::exp(x);
    return z / (1.0 + z);
}

void seed_perp_reducible_quantities(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state,
    std::vector<double>& long_qty,
    std::vector<double>& short_qty)
{
    if (runtime_state.position_symbol_id_by_slot.size() != runtime_state.positions.size()) {
        runtime_state.position_symbol_id_by_slot.assign(
            runtime_state.positions.size(),
            std::numeric_limits<size_t>::max());
    }
    for (size_t i = 0; i < runtime_state.positions.size(); ++i) {
        const auto& position = runtime_state.positions[i];
        if (position.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp) {
            continue;
        }
        size_t idx = runtime_state.position_symbol_id_by_slot[i];
        if (idx != std::numeric_limits<size_t>::max()) {
            runtime_state.position_symbol_id_by_position_id[position.id] = idx;
        }
        else {
            const auto symbol_it = step_state.symbol_to_id.find(position.symbol);
            if (symbol_it == step_state.symbol_to_id.end()) {
                continue;
            }
            idx = symbol_it->second;
            runtime_state.position_symbol_id_by_position_id[position.id] = idx;
            runtime_state.position_symbol_id_by_slot[i] = idx;
        }
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

bool is_first_matching_step(
    const QTrading::dto::Order& order,
    const State::StepKernelState& step_state) noexcept
{
    return order.first_matching_step == 0 || order.first_matching_step == step_state.step_seq;
}

bool is_one_step_limit_tif(QTrading::Dto::Trading::TimeInForce tif) noexcept
{
    return tif == QTrading::Dto::Trading::TimeInForce::IOC ||
        tif == QTrading::Dto::Trading::TimeInForce::FOK;
}

double compute_fill_price(const QTrading::dto::Order& order, const QTrading::Dto::Market::Binance::TradeKlineDto& kline)
{
    if (order.price <= 0.0) {
        return kline.ClosePrice;
    }
    return order.price;
}

double apply_execution_slippage(
    const QTrading::dto::Order& order,
    const QTrading::Dto::Market::Binance::TradeKlineDto& kline,
    const Config::SimulationConfig& config,
    double raw_price) noexcept
{
    if (!(raw_price > 0.0)) {
        return raw_price;
    }
    if (order.price <= 0.0) {
        const double slip = std::max(0.0, config.market_execution_slippage);
        if (slip <= 0.0) {
            return raw_price;
        }
        if (order.side == QTrading::Dto::Trading::OrderSide::Buy) {
            return std::min(kline.HighPrice, raw_price * (1.0 + slip));
        }
        return std::max(kline.LowPrice, raw_price * (1.0 - slip));
    }

    const double slip = std::max(0.0, config.limit_execution_slippage);
    if (slip <= 0.0) {
        return raw_price;
    }
    if (order.side == QTrading::Dto::Trading::OrderSide::Buy) {
        const double worsened = std::min(kline.HighPrice, kline.ClosePrice * (1.0 + slip));
        return std::min(order.price, worsened);
    }
    const double worsened = std::max(kline.LowPrice, kline.ClosePrice * (1.0 - slip));
    return std::max(order.price, worsened);
}

double apply_market_impact_slippage(
    const QTrading::dto::Order& order,
    const QTrading::Dto::Market::Binance::TradeKlineDto& kline,
    const Config::SimulationConfig& config,
    double quantity,
    double available_liquidity,
    double raw_price,
    double& out_impact_bps) noexcept
{
    out_impact_bps = 0.0;
    if (!(raw_price > 0.0) || quantity <= kEpsilon || !config.market_impact_slippage_enabled) {
        return raw_price;
    }
    const double denom = std::max(available_liquidity + std::max(0.0, config.market_impact_liquidity_bias), kEpsilon);
    const double size_ratio = std::max(0.0, quantity / denom);
    const double exponent = std::max(0.1, config.market_impact_size_exponent);
    double impact_bps = std::max(0.0, config.market_impact_base_bps) +
        std::max(0.0, config.market_impact_max_bps) * std::pow(size_ratio, exponent) +
        std::max(0.0, config.market_impact_offset_bps);
    const double max_bps = std::max(0.0, config.market_impact_max_bps);
    if (max_bps > 0.0) {
        impact_bps = std::min(impact_bps, max_bps);
    }
    out_impact_bps = impact_bps;
    const double ratio = impact_bps / 10000.0;
    if (order.side == QTrading::Dto::Trading::OrderSide::Buy) {
        double impacted = raw_price * (1.0 + ratio);
        impacted = std::min(impacted, kline.HighPrice);
        if (order.price > 0.0) {
            impacted = std::min(impacted, order.price);
        }
        return impacted;
    }
    double impacted = raw_price * (1.0 - ratio);
    impacted = std::max(impacted, kline.LowPrice);
    if (order.price > 0.0) {
        impacted = std::max(impacted, order.price);
    }
    return impacted;
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

double compute_penetration_ratio(const QTrading::dto::Order& order, const QTrading::Dto::Market::Binance::TradeKlineDto& kline) noexcept
{
    const double range = kline.HighPrice - kline.LowPrice;
    if (range <= kEpsilon || order.price <= 0.0) {
        return 1.0;
    }
    if (order.side == QTrading::Dto::Trading::OrderSide::Buy) {
        return clamp01((order.price - kline.LowPrice) / range);
    }
    return clamp01((kline.HighPrice - order.price) / range);
}

double compute_limit_fill_probability(
    const QTrading::dto::Order& order,
    const QTrading::Dto::Market::Binance::TradeKlineDto& kline,
    const Config::SimulationConfig& config,
    double available_liquidity,
    double taker_buy_ratio) noexcept
{
    if (!config.limit_fill_probability_enabled || order.price <= 0.0) {
        return 1.0;
    }
    const double penetration = compute_penetration_ratio(order, kline);
    const double size_ratio = clamp01(order.quantity / std::max(available_liquidity, 1.0));
    const double interaction = penetration * size_ratio;
    const double z =
        config.limit_fill_probability_bias +
        config.limit_fill_probability_penetration_weight * penetration -
        config.limit_fill_probability_size_weight * size_ratio +
        config.limit_fill_probability_taker_weight * taker_buy_ratio +
        config.limit_fill_probability_interaction_weight * interaction;
    return std::min(clamp01(sigmoid(z)), 0.999999);
}

double compute_taker_probability(
    const QTrading::dto::Order& order,
    const QTrading::Dto::Market::Binance::TradeKlineDto& kline,
    const Config::SimulationConfig& config,
    double available_liquidity,
    double taker_buy_ratio) noexcept
{
    if (!config.taker_probability_model_enabled) {
        return order.price <= 0.0 ? 1.0 : 0.0;
    }
    const double penetration = compute_penetration_ratio(order, kline);
    const double size_ratio = clamp01(order.quantity / std::max(available_liquidity, 1.0));
    const double interaction = penetration * taker_buy_ratio;
    const double z =
        config.taker_probability_bias +
        config.taker_probability_penetration_weight * penetration +
        config.taker_probability_size_weight * size_ratio +
        config.taker_probability_taker_weight * taker_buy_ratio +
        config.taker_probability_interaction_weight * interaction;
    return clamp01(sigmoid(z));
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

size_t lane_index(size_t symbol_index, QTrading::Dto::Trading::OrderSide side) noexcept
{
    return symbol_index * 2 + (side == QTrading::Dto::Trading::OrderSide::Sell ? 1U : 0U);
}

bool order_priority_less(
    const std::vector<QTrading::dto::Order>& orders,
    size_t lhs_idx,
    size_t rhs_idx) noexcept
{
    const auto& lhs = orders[lhs_idx];
    const auto& rhs = orders[rhs_idx];
    if (lhs.price <= 0.0 || rhs.price <= 0.0) {
        return lhs_idx < rhs_idx;
    }
    if (lhs.side == QTrading::Dto::Trading::OrderSide::Buy) {
        if (std::abs(lhs.price - rhs.price) > kEpsilon) {
            return lhs.price > rhs.price;
        }
    }
    else {
        if (std::abs(lhs.price - rhs.price) > kEpsilon) {
            return lhs.price < rhs.price;
        }
    }
    return lhs.id < rhs.id;
}

void insert_order_into_lane(
    std::vector<size_t>& lane,
    const std::vector<QTrading::dto::Order>& orders,
    size_t order_idx)
{
    const auto insert_it = std::lower_bound(
        lane.begin(),
        lane.end(),
        order_idx,
        [&](size_t lhs_idx, size_t rhs_idx) {
            return order_priority_less(orders, lhs_idx, rhs_idx);
        });
    lane.insert(insert_it, order_idx);
}

bool should_sync_trade_soa_from_payload(
    const State::StepKernelState& step_state,
    const QTrading::Dto::Market::Binance::MultiKlineDto& market) noexcept
{
    const size_t payload_symbols = market.trade_klines_by_id.size();
    if (payload_symbols == 0) {
        return false;
    }
    if (step_state.replay_has_trade_kline_by_symbol.size() != payload_symbols ||
        step_state.replay_trade_open_by_symbol.size() != payload_symbols ||
        step_state.replay_trade_high_by_symbol.size() != payload_symbols ||
        step_state.replay_trade_low_by_symbol.size() != payload_symbols ||
        step_state.replay_trade_close_by_symbol.size() != payload_symbols ||
        step_state.replay_trade_volume_by_symbol.size() != payload_symbols ||
        step_state.replay_trade_taker_buy_base_volume_by_symbol.size() != payload_symbols) {
        return true;
    }
    return market.symbols != step_state.symbols_shared;
}

void sync_trade_soa_from_payload(
    State::StepKernelState& step_state,
    const QTrading::Dto::Market::Binance::MultiKlineDto& market)
{
    const size_t payload_symbols = market.trade_klines_by_id.size();
    step_state.replay_has_trade_kline_by_symbol.assign(payload_symbols, 0);
    step_state.replay_trade_open_by_symbol.assign(payload_symbols, 0.0);
    step_state.replay_trade_high_by_symbol.assign(payload_symbols, 0.0);
    step_state.replay_trade_low_by_symbol.assign(payload_symbols, 0.0);
    step_state.replay_trade_close_by_symbol.assign(payload_symbols, 0.0);
    step_state.replay_trade_volume_by_symbol.assign(payload_symbols, 0.0);
    step_state.replay_trade_taker_buy_base_volume_by_symbol.assign(payload_symbols, 0.0);
    for (size_t i = 0; i < payload_symbols; ++i) {
        if (!market.trade_klines_by_id[i].has_value()) {
            continue;
        }
        const auto& kline = *market.trade_klines_by_id[i];
        step_state.replay_has_trade_kline_by_symbol[i] = 1;
        step_state.replay_trade_open_by_symbol[i] = kline.OpenPrice;
        step_state.replay_trade_high_by_symbol[i] = kline.HighPrice;
        step_state.replay_trade_low_by_symbol[i] = kline.LowPrice;
        step_state.replay_trade_close_by_symbol[i] = kline.ClosePrice;
        step_state.replay_trade_volume_by_symbol[i] = kline.Volume;
        step_state.replay_trade_taker_buy_base_volume_by_symbol[i] = kline.TakerBuyBaseVolume;
    }
}

} // namespace

void MatchingEngine::RunStep(
    State::BinanceExchangeRuntimeState& runtime_state,
    State::StepKernelState& step_state,
    const QTrading::Dto::Market::Binance::MultiKlineDto& market,
    std::vector<MatchFill>& out_fills)
{
    out_fills.clear();
    if (runtime_state.orders.empty()) {
        return;
    }
    if (should_sync_trade_soa_from_payload(step_state, market)) {
        sync_trade_soa_from_payload(step_state, market);
    }
    const size_t symbol_count = step_state.replay_has_trade_kline_by_symbol.size();
    if (symbol_count == 0) {
        return;
    }

    if (out_fills.capacity() < runtime_state.orders.size()) {
        out_fills.reserve(runtime_state.orders.size());
    }

    auto& liquidity_left = step_state.matching_liquidity_scratch;
    auto& buy_liquidity_left = step_state.matching_buy_liquidity_scratch;
    auto& sell_liquidity_left = step_state.matching_sell_liquidity_scratch;
    auto& taker_buy_ratio_by_symbol = step_state.matching_taker_buy_ratio_scratch;
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
    liquidity_left.assign(symbol_count, 0.0);
    buy_liquidity_left.assign(symbol_count, 0.0);
    sell_liquidity_left.assign(symbol_count, 0.0);
    taker_buy_ratio_by_symbol.assign(symbol_count, 0.0);
    has_liquidity.assign(symbol_count, 0);
    reducible_long_qty.assign(symbol_count, 0.0);
    reducible_short_qty.assign(symbol_count, 0.0);
    seed_perp_reducible_quantities(runtime_state, step_state, reducible_long_qty, reducible_short_qty);
    for (size_t i = 0; i < symbol_count; ++i) {
        if (i >= step_state.replay_has_trade_kline_by_symbol.size() ||
            step_state.replay_has_trade_kline_by_symbol[i] == 0) {
            continue;
        }
        QTrading::Dto::Market::Binance::TradeKlineDto kline{};
        kline.Timestamp = market.Timestamp;
        kline.OpenPrice = step_state.replay_trade_open_by_symbol[i];
        kline.HighPrice = step_state.replay_trade_high_by_symbol[i];
        kline.LowPrice = step_state.replay_trade_low_by_symbol[i];
        kline.ClosePrice = step_state.replay_trade_close_by_symbol[i];
        kline.Volume = step_state.replay_trade_volume_by_symbol[i];
        kline.TakerBuyBaseVolume = step_state.replay_trade_taker_buy_base_volume_by_symbol[i];
        const double raw = kline.Volume;
        const double base_liquidity = raw > 0.0 ? raw : 0.0;
        const double taker_buy_ratio = resolve_taker_buy_ratio(
            kline,
            runtime_state.simulation_config,
            market.Timestamp,
            i);
        taker_buy_ratio_by_symbol[i] = taker_buy_ratio;
        liquidity_left[i] = base_liquidity;
        if (opposite_passive_split && std::isfinite(base_liquidity)) {
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
    auto& order_symbol_ids_by_slot = runtime_state.order_symbol_id_by_slot;
    auto& next_orders = step_state.matching_orders_next_scratch;
    next_orders.clear();
    next_orders.reserve(orders.size());
    auto& next_order_symbol_ids = step_state.matching_order_index_scratch;
    next_order_symbol_ids.clear();
    next_order_symbol_ids.reserve(orders.size());
    if (order_symbol_ids_by_slot.size() != orders.size()) {
        order_symbol_ids_by_slot.assign(orders.size(), std::numeric_limits<size_t>::max());
    }
    auto& remaining_qty = step_state.matching_order_remaining_qty_scratch;
    auto& order_symbol_ids = step_state.matching_order_symbol_id_scratch;
    auto& has_order_symbol_id = step_state.matching_order_has_symbol_id_scratch;
    const size_t lane_count = symbol_count * 2;
    auto& order_lanes = step_state.matching_order_lanes_scratch;
    if (order_lanes.size() != lane_count) {
        order_lanes.assign(lane_count, {});
    }
    for (auto& lane : order_lanes) {
        lane.clear();
    }
    remaining_qty.resize(orders.size(), 0.0);
    order_symbol_ids.resize(orders.size(), std::numeric_limits<size_t>::max());
    has_order_symbol_id.resize(orders.size(), 0);
    for (size_t i = 0; i < orders.size(); ++i) {
        remaining_qty[i] = std::max(0.0, orders[i].quantity);
        size_t symbol_index = order_symbol_ids_by_slot[i];
        if (symbol_index != std::numeric_limits<size_t>::max()) {
            runtime_state.order_symbol_id_by_order_id[orders[i].id] = symbol_index;
        }
        else {
            const auto symbol_it = step_state.symbol_to_id.find(orders[i].symbol);
            if (symbol_it == step_state.symbol_to_id.end()) {
                continue;
            }
            symbol_index = symbol_it->second;
            runtime_state.order_symbol_id_by_order_id[orders[i].id] = symbol_index;
            order_symbol_ids_by_slot[i] = symbol_index;
        }
        if (symbol_index == std::numeric_limits<size_t>::max() ||
            symbol_index >= step_state.replay_has_trade_kline_by_symbol.size() ||
            step_state.replay_has_trade_kline_by_symbol[symbol_index] == 0 ||
            !has_liquidity[symbol_index]) {
            continue;
        }
        has_order_symbol_id[i] = 1;
        order_symbol_ids[i] = symbol_index;
        const size_t idx = lane_index(symbol_index, orders[i].side);
        if (idx >= order_lanes.size()) {
            continue;
        }
        insert_order_into_lane(order_lanes[idx], orders, i);
    }

    for (size_t symbol_index = 0; symbol_index < symbol_count; ++symbol_index) {
        if (symbol_index >= step_state.replay_has_trade_kline_by_symbol.size() ||
            step_state.replay_has_trade_kline_by_symbol[symbol_index] == 0) {
            continue;
        }
        QTrading::Dto::Market::Binance::TradeKlineDto kline{};
        kline.Timestamp = market.Timestamp;
        kline.OpenPrice = step_state.replay_trade_open_by_symbol[symbol_index];
        kline.HighPrice = step_state.replay_trade_high_by_symbol[symbol_index];
        kline.LowPrice = step_state.replay_trade_low_by_symbol[symbol_index];
        kline.ClosePrice = step_state.replay_trade_close_by_symbol[symbol_index];
        kline.Volume = step_state.replay_trade_volume_by_symbol[symbol_index];
        kline.TakerBuyBaseVolume = step_state.replay_trade_taker_buy_base_volume_by_symbol[symbol_index];
        const double taker_buy_ratio = taker_buy_ratio_by_symbol[symbol_index];
        for (size_t side_lane = 0; side_lane < 2; ++side_lane) {
            const size_t idx = symbol_index * 2 + side_lane;
            if (idx >= order_lanes.size()) {
                continue;
            }
            auto& lane = order_lanes[idx];
            for (const size_t order_idx : lane) {
                if (order_idx >= orders.size() ||
                    order_idx >= remaining_qty.size() ||
                    order_idx >= has_order_symbol_id.size() ||
                    order_idx >= order_symbol_ids.size() ||
                    has_order_symbol_id[order_idx] == 0 ||
                    order_symbol_ids[order_idx] != symbol_index ||
                    remaining_qty[order_idx] <= kEpsilon) {
                    continue;
                }
                const auto& order = orders[order_idx];
                if (is_one_step_limit_tif(order.time_in_force) &&
                    !is_first_matching_step(order, step_state)) {
                    continue;
                }
                if (!is_marketable(order, kline)) {
                    continue;
                }
                if (liquidity_left[symbol_index] <= kEpsilon) {
                    continue;
                }

                const double fill_price = compute_fill_price(order, kline);
                const bool is_taker = order.price <= 0.0 ||
                    (open_marketability_path
                        ? is_marketable_at_open(order, kline)
                        : (order.side == QTrading::Dto::Trading::OrderSide::Buy
                            ? kline.ClosePrice <= order.price + kEpsilon
                            : kline.ClosePrice + kEpsilon >= order.price));
                const double request_qty = remaining_qty[order_idx];
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
                        continue;
                    }
                    max_fill_qty = std::min(max_fill_qty, reducible_qty);
                }
                const double fill_probability = compute_limit_fill_probability(
                    order,
                    kline,
                    runtime_state.simulation_config,
                    std::max(available_liquidity, kEpsilon),
                    taker_buy_ratio);
                if (order.time_in_force == QTrading::Dto::Trading::TimeInForce::FOK &&
                    is_first_matching_step(order, step_state)) {
                    if (max_fill_qty + kEpsilon < request_qty ||
                        fill_probability + 1e-6 < 1.0) {
                        continue;
                    }
                }
                const double fill_qty = max_fill_qty * fill_probability;
                if (fill_qty <= kEpsilon) {
                    continue;
                }

                double fill_taker_probability = compute_taker_probability(
                    order,
                    kline,
                    runtime_state.simulation_config,
                    std::max(available_liquidity, kEpsilon),
                    taker_buy_ratio);
                if (!runtime_state.simulation_config.taker_probability_model_enabled) {
                    fill_taker_probability = is_taker ? 1.0 : 0.0;
                }
                const bool resolved_taker = runtime_state.simulation_config.taker_probability_model_enabled
                    ? (fill_taker_probability > 0.0)
                    : is_taker;
                double impact_bps = 0.0;
                double adjusted_fill_price = apply_execution_slippage(
                    order,
                    kline,
                    runtime_state.simulation_config,
                    fill_price);
                adjusted_fill_price = apply_market_impact_slippage(
                    order,
                    kline,
                    runtime_state.simulation_config,
                    fill_qty,
                    std::max(available_liquidity, kEpsilon),
                    adjusted_fill_price,
                    impact_bps);

                MatchFill fill{};
                fill.order_id = order.id;
                fill.symbol_id = symbol_index;
                fill.symbol = order.symbol;
                fill.instrument_type = order.instrument_type;
                fill.side = order.side;
                fill.position_side = order.position_side;
                fill.reduce_only = order.reduce_only;
                fill.close_position = order.close_position;
                fill.is_taker = resolved_taker;
                fill.fill_probability = fill_probability;
                fill.taker_probability = fill_taker_probability;
                fill.impact_slippage_bps = impact_bps;
                fill.quote_order_qty = order.quote_order_qty;
                fill.order_price = order.price;
                fill.closing_position_id = order.closing_position_id;
                fill.order_quantity = request_qty;
                fill.quantity = fill_qty;
                fill.price = adjusted_fill_price;
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
                remaining_qty[order_idx] = request_qty - fill_qty;
            }
        }
    }

    for (size_t i = 0; i < orders.size(); ++i) {
        if (remaining_qty[i] <= kEpsilon) {
            runtime_state.order_symbol_id_by_order_id.erase(orders[i].id);
            continue;
        }
        if (orders[i].price <= 0.0) {
            runtime_state.order_symbol_id_by_order_id.erase(orders[i].id);
            continue;
        }
        if (is_one_step_limit_tif(orders[i].time_in_force) &&
            (orders[i].first_matching_step == 0 ||
                step_state.step_seq >= orders[i].first_matching_step)) {
            runtime_state.order_symbol_id_by_order_id.erase(orders[i].id);
            continue;
        }
        auto order = std::move(orders[i]);
        order.quantity = remaining_qty[i];
        next_orders.emplace_back(std::move(order));
        next_order_symbol_ids.push_back(i < order_symbol_ids_by_slot.size()
                ? order_symbol_ids_by_slot[i]
                : std::numeric_limits<size_t>::max());
    }
    const bool order_book_mutated = !out_fills.empty() || next_orders.size() != orders.size();
    orders.swap(next_orders);
    order_symbol_ids_by_slot.swap(next_order_symbol_ids);
    if (order_book_mutated) {
        ++runtime_state.orders_version;
    }
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
