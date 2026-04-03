#include "Exchanges/BinanceSimulator/Domain/OrderEntryService.hpp"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <unordered_map>
#include <vector>

#include "Exchanges/BinanceSimulator/Account/Account.hpp"
#include "Exchanges/BinanceSimulator/Account/Config.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"
#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

namespace {

constexpr double kEpsilon = 1e-12;

double spot_buy_reservation_multiplier(const State::BinanceExchangeRuntimeState& runtime_state)
{
    if (runtime_state.simulation_config.spot_commission_mode ==
        Config::SpotCommissionMode::BaseOnBuyQuoteOnSell) {
        return 1.0;
    }
    auto it = ::spot_vip_fee_rates.find(runtime_state.vip_level);
    if (it == ::spot_vip_fee_rates.end()) {
        it = ::spot_vip_fee_rates.find(0);
    }
    return 1.0 + it->second.taker_fee_rate;
}

bool symbol_exists(
    const State::StepKernelState& step_state,
    const std::string& symbol)
{
    return step_state.symbol_to_id.find(symbol) != step_state.symbol_to_id.end();
}

size_t find_symbol_index(const State::StepKernelState& step_state, const std::string& symbol)
{
    const auto it = step_state.symbol_to_id.find(symbol);
    if (it == step_state.symbol_to_id.end()) {
        return std::numeric_limits<size_t>::max();
    }
    return it->second;
}

double current_reference_price(
    const State::StepKernelState& step_state,
    const std::string& symbol);

double current_mark_price(
    const State::StepKernelState& step_state,
    const std::string& symbol);

double current_index_price(
    const State::StepKernelState& step_state,
    const std::string& symbol);

bool is_immediately_marketable_post_only(
    const State::StepKernelState& step_state,
    const Contracts::OrderCommandRequest& request)
{
    if (request.price <= 0.0) {
        return false;
    }

    double reference = current_reference_price(step_state, request.symbol);
    if (reference <= 0.0 &&
        request.instrument_type == QTrading::Dto::Trading::InstrumentType::Perp) {
        reference = current_mark_price(step_state, request.symbol);
    }
    if (reference <= 0.0) {
        return false;
    }

    if (request.side == QTrading::Dto::Trading::OrderSide::Buy) {
        return request.price + kEpsilon >= reference;
    }
    return request.price <= reference + kEpsilon;
}

bool is_multiple_of_step(double value, double step_size)
{
    if (step_size <= 0.0) {
        return true;
    }
    const double steps = std::round(value / step_size);
    return std::abs((steps * step_size) - value) <= 1e-9;
}

bool validate_filters(
    const State::StepKernelState& step_state,
    const Contracts::OrderCommandRequest& request,
    double quantity,
    std::optional<Contracts::OrderRejectInfo>& reject)
{
    const size_t symbol_index = find_symbol_index(step_state, request.symbol);
    if (symbol_index == std::numeric_limits<size_t>::max() ||
        symbol_index >= step_state.symbol_spec_by_id.size()) {
        return true;
    }

    const auto& spec = step_state.symbol_spec_by_id[symbol_index];

    if (request.price > 0.0) {
        auto resolve_percent_price_reference = [&]() -> double {
            double ref = current_reference_price(step_state, request.symbol);
            if (ref <= 0.0 && request.instrument_type == QTrading::Dto::Trading::InstrumentType::Perp) {
                ref = current_mark_price(step_state, request.symbol);
            }
            return ref;
        };

        if (spec.percent_price_by_side) {
            const bool is_buy = request.side == QTrading::Dto::Trading::OrderSide::Buy;
            const double multiplier_up = is_buy ? spec.bid_multiplier_up : spec.ask_multiplier_up;
            const double multiplier_down = is_buy ? spec.bid_multiplier_down : spec.ask_multiplier_down;
            if (multiplier_up > 0.0 || multiplier_down > 0.0) {
                const double reference = resolve_percent_price_reference();
                if (reference > 0.0) {
                    if (multiplier_up > 0.0 && request.price - kEpsilon > reference * multiplier_up) {
                        reject = Contracts::OrderRejectInfo{
                            Contracts::OrderRejectInfo::Code::PercentPriceAboveBound,
                            "price above percent price upper bound" };
                        return false;
                    }
                    if (multiplier_down > 0.0 && request.price + kEpsilon < reference * multiplier_down) {
                        reject = Contracts::OrderRejectInfo{
                            Contracts::OrderRejectInfo::Code::PercentPriceBelowBound,
                            "price below percent price lower bound" };
                        return false;
                    }
                }
            }
        }
        else if (spec.percent_price_multiplier_up > 0.0 || spec.percent_price_multiplier_down > 0.0) {
            const double reference = resolve_percent_price_reference();
            if (reference > 0.0) {
                if (spec.percent_price_multiplier_up > 0.0 &&
                    request.price - kEpsilon > reference * spec.percent_price_multiplier_up) {
                    reject = Contracts::OrderRejectInfo{
                        Contracts::OrderRejectInfo::Code::PercentPriceAboveBound,
                        "price above percent price upper bound" };
                    return false;
                }
                if (spec.percent_price_multiplier_down > 0.0 &&
                    request.price + kEpsilon < reference * spec.percent_price_multiplier_down) {
                    reject = Contracts::OrderRejectInfo{
                        Contracts::OrderRejectInfo::Code::PercentPriceBelowBound,
                        "price below percent price lower bound" };
                    return false;
                }
            }
        }

        if (spec.min_price > 0.0 && request.price + kEpsilon < spec.min_price) {
            reject = Contracts::OrderRejectInfo{
                Contracts::OrderRejectInfo::Code::PriceFilterBelowMin,
                "price below min" };
            return false;
        }
        if (spec.max_price > 0.0 && request.price - kEpsilon > spec.max_price) {
            reject = Contracts::OrderRejectInfo{
                Contracts::OrderRejectInfo::Code::PriceFilterAboveMax,
                "price above max" };
            return false;
        }
        if (spec.price_tick_size > 0.0 && !is_multiple_of_step(request.price, spec.price_tick_size)) {
            reject = Contracts::OrderRejectInfo{
                Contracts::OrderRejectInfo::Code::PriceFilterInvalidTick,
                "invalid price tick" };
            return false;
        }
    }

    const bool is_market = request.price <= 0.0;
    const double min_qty = is_market && spec.market_min_qty > 0.0 ? spec.market_min_qty : spec.min_qty;
    const double max_qty = is_market && spec.market_max_qty > 0.0 ? spec.market_max_qty : spec.max_qty;
    const double qty_step = is_market && spec.market_qty_step_size > 0.0 ? spec.market_qty_step_size : spec.qty_step_size;

    if (min_qty > 0.0 && quantity + kEpsilon < min_qty) {
        reject = Contracts::OrderRejectInfo{
            Contracts::OrderRejectInfo::Code::LotSizeBelowMinQty,
            "quantity below min" };
        return false;
    }
    if (max_qty > 0.0 && quantity - kEpsilon > max_qty) {
        reject = Contracts::OrderRejectInfo{
            Contracts::OrderRejectInfo::Code::LotSizeAboveMaxQty,
            "quantity above max" };
        return false;
    }
    if (qty_step > 0.0 && !is_multiple_of_step(quantity, qty_step)) {
        reject = Contracts::OrderRejectInfo{
            Contracts::OrderRejectInfo::Code::LotSizeInvalidStep,
            "invalid quantity step" };
        return false;
    }

    if (spec.min_notional > 0.0 || spec.max_notional > 0.0) {
        double ref_price = 0.0;
        if (request.price > 0.0) {
            ref_price = request.price;
        }
        else if (request.instrument_type == QTrading::Dto::Trading::InstrumentType::Perp) {
            ref_price = current_mark_price(step_state, request.symbol);
            if (ref_price <= 0.0) {
                ref_price = current_reference_price(step_state, request.symbol);
            }
        }
        else {
            ref_price = current_reference_price(step_state, request.symbol);
        }
        if (ref_price <= 0.0) {
            reject = Contracts::OrderRejectInfo{
                Contracts::OrderRejectInfo::Code::NotionalNoReferencePrice,
                "missing reference price for notional filter" };
            return false;
        }
        const double notional = quantity * ref_price;
        if (spec.min_notional > 0.0 && notional + kEpsilon < spec.min_notional) {
            reject = Contracts::OrderRejectInfo{
                Contracts::OrderRejectInfo::Code::NotionalBelowMin,
                "notional below min" };
            return false;
        }
        if (spec.max_notional > 0.0 && notional - kEpsilon > spec.max_notional) {
            reject = Contracts::OrderRejectInfo{
                Contracts::OrderRejectInfo::Code::NotionalAboveMax,
                "notional above max" };
            return false;
        }
    }

    return true;
}

double current_reference_price(
    const State::StepKernelState& step_state,
    const std::string& symbol)
{
    const size_t symbol_index = find_symbol_index(step_state, symbol);
    if (symbol_index == std::numeric_limits<size_t>::max() ||
        symbol_index >= step_state.market_data.size()) {
        return 0.0;
    }
    const auto& market_data = step_state.market_data[symbol_index];
    const size_t count = market_data.get_klines_count();
    if (count == 0) {
        return 0.0;
    }

    const size_t cursor = symbol_index < step_state.replay_cursor.size()
        ? step_state.replay_cursor[symbol_index]
        : 0;
    if (cursor == 0) {
        return market_data.get_kline(0).OpenPrice;
    }

    const size_t idx = std::min(cursor - 1, count - 1);
    return market_data.get_kline(idx).ClosePrice;
}

double current_mark_price(
    const State::StepKernelState& step_state,
    const std::string& symbol)
{
    const size_t symbol_index = find_symbol_index(step_state, symbol);
    if (symbol_index == std::numeric_limits<size_t>::max() ||
        symbol_index >= step_state.mark_data_id_by_symbol.size() ||
        symbol_index >= step_state.mark_cursor_by_symbol.size()) {
        return 0.0;
    }

    const int32_t mark_data_id = step_state.mark_data_id_by_symbol[symbol_index];
    if (mark_data_id < 0 ||
        static_cast<size_t>(mark_data_id) >= step_state.mark_data_pool.size()) {
        return 0.0;
    }

    const auto& mark_data = step_state.mark_data_pool[static_cast<size_t>(mark_data_id)];
    const size_t count = mark_data.get_klines_count();
    if (count == 0) {
        return 0.0;
    }

    const size_t cursor = step_state.mark_cursor_by_symbol[symbol_index];
    if (cursor == 0) {
        return mark_data.get_kline(0).OpenPrice;
    }

    const size_t idx = std::min(cursor - 1, count - 1);
    return mark_data.get_kline(idx).ClosePrice;
}

double current_index_price(
    const State::StepKernelState& step_state,
    const std::string& symbol)
{
    const size_t symbol_index = find_symbol_index(step_state, symbol);
    if (symbol_index == std::numeric_limits<size_t>::max() ||
        symbol_index >= step_state.index_data_id_by_symbol.size() ||
        symbol_index >= step_state.index_cursor_by_symbol.size()) {
        return 0.0;
    }

    const int32_t index_data_id = step_state.index_data_id_by_symbol[symbol_index];
    if (index_data_id < 0 ||
        static_cast<size_t>(index_data_id) >= step_state.index_data_pool.size()) {
        return 0.0;
    }

    const auto& index_data = step_state.index_data_pool[static_cast<size_t>(index_data_id)];
    const size_t count = index_data.get_klines_count();
    if (count == 0) {
        return 0.0;
    }

    const size_t cursor = step_state.index_cursor_by_symbol[symbol_index];
    if (cursor == 0) {
        return index_data.get_kline(0).OpenPrice;
    }

    const size_t idx = std::min(cursor - 1, count - 1);
    return index_data.get_kline(idx).ClosePrice;
}

double sum_spot_inventory(const State::BinanceExchangeRuntimeState& runtime_state, const std::string& symbol)
{
    double inventory = 0.0;
    for (const auto& position : runtime_state.positions) {
        if (position.instrument_type == QTrading::Dto::Trading::InstrumentType::Spot &&
            position.symbol == symbol &&
            position.is_long) {
            inventory += position.quantity;
        }
    }
    return inventory;
}

double sum_spot_sell_reservations(const State::BinanceExchangeRuntimeState& runtime_state, const std::string& symbol)
{
    double reserved = 0.0;
    for (const auto& order : runtime_state.orders) {
        if (order.instrument_type == QTrading::Dto::Trading::InstrumentType::Spot &&
            order.symbol == symbol &&
            order.side == QTrading::Dto::Trading::OrderSide::Sell) {
            reserved += order.quantity;
        }
    }
    return reserved;
}

std::optional<size_t> try_resolve_order_symbol_id(
    const State::StepKernelState& step_state,
    const std::string& symbol)
{
    const auto it = step_state.symbol_to_id.find(symbol);
    if (it == step_state.symbol_to_id.end()) {
        return std::nullopt;
    }
    return it->second;
}

double compute_spot_buy_order_reservation(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const QTrading::dto::Order& order)
{
    if (order.instrument_type != QTrading::Dto::Trading::InstrumentType::Spot ||
        order.side != QTrading::Dto::Trading::OrderSide::Buy) {
        return 0.0;
    }
    const double price = order.price > 0.0 ? order.price : order.quote_order_qty > 0.0 ? 1.0 : 0.0;
    const double quote = order.quote_order_qty > 0.0 ? order.quote_order_qty : order.quantity * price;
    return quote * spot_buy_reservation_multiplier(runtime_state);
}

void ensure_order_reservation_cache_shape(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state)
{
    const size_t symbol_count = step_state.symbols.size();
    if (runtime_state.spot_open_order_initial_margin_by_symbol.size() == symbol_count &&
        runtime_state.perp_open_order_initial_margin_by_symbol.size() == symbol_count &&
        runtime_state.perp_reference_price_by_symbol.size() == symbol_count &&
        runtime_state.perp_net_position_qty_by_symbol.size() == symbol_count) {
        return;
    }
    runtime_state.spot_open_order_initial_margin_by_symbol.assign(symbol_count, 0.0);
    runtime_state.perp_open_order_initial_margin_by_symbol.assign(symbol_count, 0.0);
    runtime_state.perp_reference_price_by_symbol.assign(symbol_count, 0.0);
    runtime_state.perp_net_position_qty_by_symbol.assign(symbol_count, 0.0);
    runtime_state.order_reservation_cache_ready = false;
}

void rebuild_perp_reference_price_cache(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state)
{
    std::fill(
        runtime_state.perp_reference_price_by_symbol.begin(),
        runtime_state.perp_reference_price_by_symbol.end(),
        0.0);
    for (const auto& snapshot : runtime_state.last_status_snapshot.prices) {
        const auto symbol_id = try_resolve_order_symbol_id(step_state, snapshot.symbol);
        if (!symbol_id.has_value() || *symbol_id >= runtime_state.perp_reference_price_by_symbol.size()) {
            continue;
        }
        if (snapshot.has_mark_price && snapshot.mark_price > 0.0) {
            runtime_state.perp_reference_price_by_symbol[*symbol_id] =
                snapshot.mark_price;
            continue;
        }
        if (snapshot.has_trade_price && snapshot.trade_price > 0.0) {
            runtime_state.perp_reference_price_by_symbol[*symbol_id] =
                snapshot.trade_price;
            continue;
        }
        if (snapshot.has_price && snapshot.price > 0.0) {
            runtime_state.perp_reference_price_by_symbol[*symbol_id] =
                snapshot.price;
        }
    }
}

void rebuild_perp_net_position_qty_cache(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state)
{
    std::fill(
        runtime_state.perp_net_position_qty_by_symbol.begin(),
        runtime_state.perp_net_position_qty_by_symbol.end(),
        0.0);
    for (const auto& position : runtime_state.positions) {
        if (position.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp) {
            continue;
        }
        const auto symbol_id = try_resolve_order_symbol_id(step_state, position.symbol);
        if (!symbol_id.has_value() || *symbol_id >= runtime_state.perp_net_position_qty_by_symbol.size()) {
            continue;
        }
        runtime_state.perp_net_position_qty_by_symbol[*symbol_id] += position.is_long ? position.quantity : -position.quantity;
    }
}

double recompute_spot_symbol_buy_reservation(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state,
    size_t symbol_id)
{
    double reserved = 0.0;
    for (size_t i = 0; i < runtime_state.orders.size(); ++i) {
        const auto& order = runtime_state.orders[i];
        std::optional<size_t> order_symbol_id = std::nullopt;
        if (i < runtime_state.order_symbol_id_by_slot.size() &&
            runtime_state.order_symbol_id_by_slot[i] != std::numeric_limits<size_t>::max()) {
            order_symbol_id = runtime_state.order_symbol_id_by_slot[i];
        }
        else {
            const auto cached_it = runtime_state.order_symbol_id_by_order_id.find(order.id);
            order_symbol_id = cached_it != runtime_state.order_symbol_id_by_order_id.end()
                ? std::optional<size_t>(cached_it->second)
                : try_resolve_order_symbol_id(step_state, order.symbol);
        }
        if (!order_symbol_id.has_value() || *order_symbol_id != symbol_id) {
            continue;
        }
        reserved += compute_spot_buy_order_reservation(runtime_state, order);
    }
    return reserved;
}

double recompute_perp_symbol_reservation(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state,
    size_t symbol_id)
{
    if (symbol_id >= runtime_state.perp_reference_price_by_symbol.size() ||
        symbol_id >= runtime_state.perp_net_position_qty_by_symbol.size()) {
        return 0.0;
    }

    double reserved = 0.0;
    double effective_net = runtime_state.hedge_mode ? 0.0 : runtime_state.perp_net_position_qty_by_symbol[symbol_id];
    for (size_t i = 0; i < runtime_state.orders.size(); ++i) {
        const auto& order = runtime_state.orders[i];
        if (order.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp ||
            order.reduce_only ||
            order.close_position) {
            continue;
        }
        std::optional<size_t> order_symbol_id = std::nullopt;
        if (i < runtime_state.order_symbol_id_by_slot.size() &&
            runtime_state.order_symbol_id_by_slot[i] != std::numeric_limits<size_t>::max()) {
            order_symbol_id = runtime_state.order_symbol_id_by_slot[i];
        }
        else {
            const auto cached_it = runtime_state.order_symbol_id_by_order_id.find(order.id);
            order_symbol_id = cached_it != runtime_state.order_symbol_id_by_order_id.end()
                ? std::optional<size_t>(cached_it->second)
                : try_resolve_order_symbol_id(step_state, order.symbol);
        }
        if (!order_symbol_id.has_value() || *order_symbol_id != symbol_id) {
            continue;
        }

        const double price = order.price > 0.0
            ? order.price
            : runtime_state.perp_reference_price_by_symbol[symbol_id];
        if (price <= 0.0) {
            continue;
        }

        double opening_qty = std::max(0.0, order.quantity);
        if (!runtime_state.hedge_mode && opening_qty > kEpsilon) {
            const double net_position = effective_net;
            const double signed_order = order.side == QTrading::Dto::Trading::OrderSide::Buy
                ? opening_qty
                : -opening_qty;
            if (std::abs(net_position) > kEpsilon && net_position * signed_order < 0.0) {
                const double closing_qty = std::min(std::abs(net_position), std::abs(signed_order));
                opening_qty = std::max(0.0, opening_qty - closing_qty);
            }
            effective_net = net_position + signed_order;
        }
        if (opening_qty <= kEpsilon) {
            continue;
        }

        const auto it = runtime_state.symbol_leverage.find(order.symbol);
        const double leverage = it == runtime_state.symbol_leverage.end() ? 1.0 : it->second;
        const double notional = opening_qty * price;
        reserved += leverage > 0.0 ? (notional / leverage) : notional;
    }
    return reserved;
}

double sum_perp_position_initial_margin(const State::BinanceExchangeRuntimeState& runtime_state)
{
    double total = 0.0;
    for (const auto& position : runtime_state.positions) {
        if (position.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp) {
            continue;
        }
        total += std::max(0.0, position.initial_margin);
    }
    return total;
}

double estimate_added_perp_open_order_margin(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state,
    const Contracts::OrderCommandRequest& request,
    double quantity)
{
    if (request.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp ||
        request.reduce_only ||
        request.close_position ||
        quantity <= kEpsilon) {
        return 0.0;
    }

    const auto symbol_id = try_resolve_order_symbol_id(step_state, request.symbol);
    if (!symbol_id.has_value()) {
        return 0.0;
    }

    ensure_order_reservation_cache_shape(runtime_state, step_state);
    rebuild_perp_reference_price_cache(runtime_state, step_state);
    rebuild_perp_net_position_qty_cache(runtime_state, step_state);

    const double current_symbol_margin = recompute_perp_symbol_reservation(
        runtime_state,
        step_state,
        *symbol_id);

    QTrading::dto::Order synthetic{};
    synthetic.id = -1;
    synthetic.symbol = request.symbol;
    synthetic.quantity = quantity;
    synthetic.price = request.price;
    synthetic.side = request.side;
    synthetic.position_side = request.position_side;
    synthetic.reduce_only = request.reduce_only;
    synthetic.instrument_type = request.instrument_type;
    synthetic.client_order_id = request.client_order_id;
    synthetic.stp_mode = request.stp_mode;
    synthetic.close_position = request.close_position;
    synthetic.quote_order_qty = request.quote_order_qty;
    runtime_state.orders.emplace_back(std::move(synthetic));
    const double with_candidate = recompute_perp_symbol_reservation(
        runtime_state,
        step_state,
        *symbol_id);
    runtime_state.orders.pop_back();

    return std::max(0.0, with_candidate - current_symbol_margin);
}

void refresh_order_margin_totals_from_symbol_cache(State::BinanceExchangeRuntimeState& runtime_state)
{
    runtime_state.spot_open_order_initial_margin = std::accumulate(
        runtime_state.spot_open_order_initial_margin_by_symbol.begin(),
        runtime_state.spot_open_order_initial_margin_by_symbol.end(),
        0.0);
    runtime_state.perp_open_order_initial_margin = std::accumulate(
        runtime_state.perp_open_order_initial_margin_by_symbol.begin(),
        runtime_state.perp_open_order_initial_margin_by_symbol.end(),
        0.0);
}

void initialize_order_reservation_cache(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state);

void refresh_symbol_reservations(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state,
    const std::vector<size_t>& touched_symbol_ids)
{
    if (!runtime_state.order_reservation_cache_ready) {
        initialize_order_reservation_cache(runtime_state, step_state);
        return;
    }
    ensure_order_reservation_cache_shape(runtime_state, step_state);
    rebuild_perp_reference_price_cache(runtime_state, step_state);
    rebuild_perp_net_position_qty_cache(runtime_state, step_state);

    std::vector<uint8_t> touched(
        runtime_state.spot_open_order_initial_margin_by_symbol.size(),
        0);
    for (const auto symbol_id : touched_symbol_ids) {
        if (symbol_id >= touched.size() || touched[symbol_id] != 0) {
            continue;
        }
        touched[symbol_id] = 1;
        runtime_state.spot_open_order_initial_margin_by_symbol[symbol_id] =
            recompute_spot_symbol_buy_reservation(runtime_state, step_state, symbol_id);
        runtime_state.perp_open_order_initial_margin_by_symbol[symbol_id] =
            recompute_perp_symbol_reservation(runtime_state, step_state, symbol_id);
    }
    refresh_order_margin_totals_from_symbol_cache(runtime_state);
    runtime_state.order_reservation_cache_ready = true;
}

void initialize_order_reservation_cache(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state)
{
    ensure_order_reservation_cache_shape(runtime_state, step_state);
    rebuild_perp_reference_price_cache(runtime_state, step_state);
    rebuild_perp_net_position_qty_cache(runtime_state, step_state);
    for (size_t symbol_id = 0; symbol_id < step_state.symbols.size(); ++symbol_id) {
        runtime_state.spot_open_order_initial_margin_by_symbol[symbol_id] =
            recompute_spot_symbol_buy_reservation(runtime_state, step_state, symbol_id);
        runtime_state.perp_open_order_initial_margin_by_symbol[symbol_id] =
            recompute_perp_symbol_reservation(runtime_state, step_state, symbol_id);
    }
    refresh_order_margin_totals_from_symbol_cache(runtime_state);
    runtime_state.order_reservation_cache_ready = true;
}

bool has_duplicate_client_order_id(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const Contracts::OrderCommandRequest& request)
{
    if (request.client_order_id.empty()) {
        return false;
    }
    for (const auto& order : runtime_state.orders) {
        if (order.client_order_id == request.client_order_id) {
            return true;
        }
    }
    return false;
}

bool has_open_positions_or_orders(const State::BinanceExchangeRuntimeState& runtime_state)
{
    return !runtime_state.positions.empty() || !runtime_state.orders.empty();
}

void sync_open_order_margins(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state);

void add_touched_symbol_id_for_order(
    const State::StepKernelState& step_state,
    const QTrading::dto::Order& order,
    std::vector<size_t>& out_symbol_ids)
{
    const auto symbol_id = try_resolve_order_symbol_id(step_state, order.symbol);
    if (symbol_id.has_value()) {
        out_symbol_ids.push_back(*symbol_id);
    }
}

std::optional<size_t> resolve_order_symbol_id_cached(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state,
    const QTrading::dto::Order& order)
{
    const auto cached_it = runtime_state.order_symbol_id_by_order_id.find(order.id);
    if (cached_it != runtime_state.order_symbol_id_by_order_id.end()) {
        return cached_it->second;
    }
    const auto symbol_id = try_resolve_order_symbol_id(step_state, order.symbol);
    if (symbol_id.has_value()) {
        runtime_state.order_symbol_id_by_order_id[order.id] = *symbol_id;
    }
    return symbol_id;
}

bool is_self_trade_conflict(
    const QTrading::dto::Order& resting_order,
    const Contracts::OrderCommandRequest& incoming_request)
{
    if (resting_order.symbol != incoming_request.symbol ||
        resting_order.instrument_type != incoming_request.instrument_type ||
        resting_order.side == incoming_request.side) {
        return false;
    }

    if (incoming_request.price <= 0.0 || resting_order.price <= 0.0) {
        return true;
    }

    if (incoming_request.side == QTrading::Dto::Trading::OrderSide::Buy) {
        return incoming_request.price + kEpsilon >= resting_order.price;
    }
    return incoming_request.price <= resting_order.price + kEpsilon;
}

bool remove_order_by_id(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state,
    int order_id)
{
    auto& orders = runtime_state.orders;
    auto& order_symbol_ids = runtime_state.order_symbol_id_by_slot;
    std::vector<size_t> touched_symbol_ids;
    touched_symbol_ids.reserve(1);
    if (order_symbol_ids.size() != orders.size()) {
        order_symbol_ids.assign(orders.size(), std::numeric_limits<size_t>::max());
    }
    size_t write = 0;
    bool removed = false;
    for (size_t read = 0; read < orders.size(); ++read) {
        auto& order = orders[read];
        const size_t symbol_id = read < order_symbol_ids.size()
            ? order_symbol_ids[read]
            : std::numeric_limits<size_t>::max();
        if (order.id != order_id) {
            if (write != read) {
                orders[write] = std::move(order);
                order_symbol_ids[write] = symbol_id;
            }
            ++write;
            continue;
        }
        removed = true;
        if (symbol_id != std::numeric_limits<size_t>::max()) {
            touched_symbol_ids.push_back(symbol_id);
        }
        else {
            const auto resolved_symbol_id = resolve_order_symbol_id_cached(runtime_state, step_state, order);
            if (resolved_symbol_id.has_value()) {
                touched_symbol_ids.push_back(*resolved_symbol_id);
            }
        }
        runtime_state.order_symbol_id_by_order_id.erase(order.id);
    }
    orders.resize(write);
    order_symbol_ids.resize(write);
    if (removed) {
        runtime_state.order_symbol_id_by_order_id.erase(order_id);
        ++runtime_state.orders_version;
        refresh_symbol_reservations(runtime_state, step_state, touched_symbol_ids);
        return true;
    }
    return false;
}

template <typename TPredicate>
size_t remove_orders_if(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state,
    TPredicate&& predicate)
{
    auto& orders = runtime_state.orders;
    auto& order_symbol_ids = runtime_state.order_symbol_id_by_slot;
    std::vector<size_t> touched_symbol_ids;
    touched_symbol_ids.reserve(4);
    if (order_symbol_ids.size() != orders.size()) {
        order_symbol_ids.assign(orders.size(), std::numeric_limits<size_t>::max());
    }
    size_t write = 0;
    size_t removed = 0;
    for (size_t read = 0; read < orders.size(); ++read) {
        auto& order = orders[read];
        const size_t symbol_id = read < order_symbol_ids.size()
            ? order_symbol_ids[read]
            : std::numeric_limits<size_t>::max();
        if (!predicate(order)) {
            if (write != read) {
                orders[write] = std::move(order);
                order_symbol_ids[write] = symbol_id;
            }
            ++write;
            continue;
        }
        ++removed;
        if (symbol_id != std::numeric_limits<size_t>::max()) {
            touched_symbol_ids.push_back(symbol_id);
        }
        else {
            const auto resolved_symbol_id = resolve_order_symbol_id_cached(runtime_state, step_state, order);
            if (resolved_symbol_id.has_value()) {
                touched_symbol_ids.push_back(*resolved_symbol_id);
            }
        }
        runtime_state.order_symbol_id_by_order_id.erase(order.id);
    }
    orders.resize(write);
    order_symbol_ids.resize(write);
    if (removed > 0) {
        ++runtime_state.orders_version;
        refresh_symbol_reservations(runtime_state, step_state, touched_symbol_ids);
    }
    return removed;
}

void sync_open_order_margins(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state)
{
    initialize_order_reservation_cache(runtime_state, step_state);
}

const QTrading::dto::Position* find_perp_position(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const std::string& symbol,
    std::optional<bool> is_long)
{
    for (const auto& position : runtime_state.positions) {
        if (position.instrument_type == QTrading::Dto::Trading::InstrumentType::Perp &&
            position.symbol == symbol &&
            (!is_long.has_value() || position.is_long == *is_long)) {
            return &position;
        }
    }
    return nullptr;
}

double reducible_perp_quantity(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const Contracts::OrderCommandRequest& request)
{
    const std::optional<bool> target_is_long = request.position_side == QTrading::Dto::Trading::PositionSide::Long
        ? std::optional<bool>(true)
        : request.position_side == QTrading::Dto::Trading::PositionSide::Short
              ? std::optional<bool>(false)
              : std::optional<bool>();
    const auto* position = find_perp_position(runtime_state, request.symbol, target_is_long);
    if (!position || position->quantity <= kEpsilon) {
        return 0.0;
    }

    const bool order_is_buy = request.side == QTrading::Dto::Trading::OrderSide::Buy;
    const bool reduces = (position->is_long && !order_is_buy) || (!position->is_long && order_is_buy);
    if (!reduces) {
        return 0.0;
    }
    return position->quantity;
}

std::optional<double> current_basis_bps(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state,
    const std::string& symbol)
{
    const auto symbol_id = try_resolve_order_symbol_id(step_state, symbol);
    if (!symbol_id.has_value() ||
        *symbol_id >= runtime_state.last_status_snapshot.prices.size()) {
        return std::nullopt;
    }
    const auto& snap = runtime_state.last_status_snapshot.prices[*symbol_id];
    if (!snap.has_mark_price ||
        !snap.has_index_price ||
        std::abs(snap.index_price) <= kEpsilon) {
        return std::nullopt;
    }
    return std::abs((snap.mark_price - snap.index_price) / snap.index_price) * 10000.0;
}

bool request_opens_target(
    const Contracts::OrderCommandRequest& request,
    bool target_is_long)
{
    return (target_is_long && request.side == QTrading::Dto::Trading::OrderSide::Buy) ||
        (!target_is_long && request.side == QTrading::Dto::Trading::OrderSide::Sell);
}

bool request_closes_target(
    const Contracts::OrderCommandRequest& request,
    bool target_is_long)
{
    return (target_is_long && request.side == QTrading::Dto::Trading::OrderSide::Sell) ||
        (!target_is_long && request.side == QTrading::Dto::Trading::OrderSide::Buy);
}

constexpr uint8_t stp_mode_bit(Account::SelfTradePreventionMode mode)
{
    return static_cast<uint8_t>(1u << static_cast<uint8_t>(mode));
}

bool is_supported_stp_mode_value(int mode_value)
{
    return mode_value >= static_cast<int>(Account::SelfTradePreventionMode::None) &&
        mode_value <= static_cast<int>(Account::SelfTradePreventionMode::ExpireBoth);
}

bool resolve_effective_stp_mode(
    const State::StepKernelState& step_state,
    const Contracts::OrderCommandRequest& request,
    Account::SelfTradePreventionMode& effective_mode,
    std::optional<Contracts::OrderRejectInfo>& reject)
{
    if (!is_supported_stp_mode_value(request.stp_mode)) {
        reject = Contracts::OrderRejectInfo{
            Contracts::OrderRejectInfo::Code::InvalidStpMode,
            "invalid selfTradePreventionMode" };
        return false;
    }

    const size_t symbol_index = find_symbol_index(step_state, request.symbol);
    const auto& spec = symbol_index < step_state.symbol_spec_by_id.size()
        ? step_state.symbol_spec_by_id[symbol_index]
        : QTrading::Dto::Trading::PerpInstrumentSpec();

    int mode_value = request.stp_mode;
    if (mode_value == static_cast<int>(Account::SelfTradePreventionMode::None)) {
        mode_value = spec.default_stp_mode;
    }
    if (!is_supported_stp_mode_value(mode_value)) {
        reject = Contracts::OrderRejectInfo{
            Contracts::OrderRejectInfo::Code::InvalidStpMode,
            "invalid default selfTradePreventionMode" };
        return false;
    }

    const auto mode = static_cast<Account::SelfTradePreventionMode>(mode_value);
    const uint8_t allowed_mask = spec.allowed_stp_modes_mask;
    if ((allowed_mask & stp_mode_bit(mode)) == 0) {
        reject = Contracts::OrderRejectInfo{
            Contracts::OrderRejectInfo::Code::StpModeNotAllowed,
            "selfTradePreventionMode not allowed for symbol" };
        return false;
    }

    effective_mode = mode;
    return true;
}

bool validate_perp_protection_constraints(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state,
    const Contracts::OrderCommandRequest& request,
    std::optional<Contracts::OrderRejectInfo>& reject)
{
    if (request.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp || request.price > 0.0) {
        return true;
    }

    const size_t symbol_index = find_symbol_index(step_state, request.symbol);
    if (symbol_index == std::numeric_limits<size_t>::max() ||
        symbol_index >= step_state.symbol_spec_by_id.size()) {
        return true;
    }
    const auto& spec = step_state.symbol_spec_by_id[symbol_index];
    const auto* snapshot = symbol_index < runtime_state.last_status_snapshot.prices.size()
        ? &runtime_state.last_status_snapshot.prices[symbol_index]
        : nullptr;

    if (spec.market_take_bound > 0.0) {
        double mark_price = 0.0;
        double market_reference = 0.0;
        if (snapshot != nullptr) {
            mark_price = snapshot->has_mark_price ? snapshot->mark_price : 0.0;
            if (snapshot->has_trade_price && snapshot->trade_price > 0.0) {
                market_reference = snapshot->trade_price;
            }
            else if (snapshot->has_price && snapshot->price > 0.0) {
                market_reference = snapshot->price;
            }
        }
        if (mark_price <= 0.0) {
            mark_price = current_mark_price(step_state, request.symbol);
        }
        if (market_reference <= 0.0) {
            market_reference = current_reference_price(step_state, request.symbol);
        }
        if (mark_price > 0.0 &&
            market_reference > 0.0 &&
            std::abs(market_reference - mark_price) / mark_price > spec.market_take_bound + kEpsilon) {
            reject = Contracts::OrderRejectInfo{
                Contracts::OrderRejectInfo::Code::MarketTakeBoundExceeded,
                "market order exceeds marketTakeBound" };
            return false;
        }
    }

    if (spec.trigger_protect > 0.0) {
        double mark_price = 0.0;
        double index_price = 0.0;
        if (snapshot != nullptr) {
            mark_price = snapshot->has_mark_price ? snapshot->mark_price : 0.0;
            index_price = snapshot->has_index_price ? snapshot->index_price : 0.0;
        }
        if (mark_price <= 0.0) {
            mark_price = current_mark_price(step_state, request.symbol);
        }
        if (index_price <= 0.0) {
            index_price = current_index_price(step_state, request.symbol);
        }
        if (mark_price > 0.0 &&
            std::abs(index_price) > kEpsilon &&
            std::abs(mark_price - index_price) / std::abs(index_price) > spec.trigger_protect + kEpsilon) {
            reject = Contracts::OrderRejectInfo{
                Contracts::OrderRejectInfo::Code::TriggerProtectExceeded,
                "order blocked by triggerProtect" };
            return false;
        }
    }

    return true;
}

} // namespace

bool OrderEntryService::SetPositionMode(
    State::BinanceExchangeRuntimeState& runtime_state,
    State::StepKernelState& step_state,
    bool hedge_mode,
    std::optional<Contracts::OrderRejectInfo>& reject)
{
    reject.reset();
    if (runtime_state.hedge_mode == hedge_mode) {
        return true;
    }
    if (has_open_positions_or_orders(runtime_state)) {
        reject = Contracts::OrderRejectInfo{
            Contracts::OrderRejectInfo::Code::Unknown,
            "cannot switch position mode with open positions or orders" };
        return false;
    }
    runtime_state.hedge_mode = hedge_mode;
    ++step_state.account_state_version;
    return true;
}

bool OrderEntryService::Execute(
    State::BinanceExchangeRuntimeState& runtime_state,
    Account& account,
    State::StepKernelState& step_state,
    const Contracts::OrderCommandRequest& request,
    std::optional<Contracts::OrderRejectInfo>& reject)
{
    reject.reset();

    if (!symbol_exists(step_state, request.symbol)) {
        reject = Contracts::OrderRejectInfo{ Contracts::OrderRejectInfo::Code::UnknownSymbol, "unknown symbol" };
        return false;
    }

    double quantity = request.quantity;
    if (request.kind == Contracts::OrderCommandKind::SpotMarketQuote) {
        const double ref_price = current_reference_price(step_state, request.symbol);
        if (ref_price <= 0.0 || request.quote_order_qty <= 0.0) {
            reject = Contracts::OrderRejectInfo{
                Contracts::OrderRejectInfo::Code::NotionalNoReferencePrice,
                "missing reference price for quote market order" };
            return false;
        }
        quantity = request.quote_order_qty / ref_price;
    }
    if (request.kind != Contracts::OrderCommandKind::PerpClosePosition &&
        quantity <= 0.0) {
        reject = Contracts::OrderRejectInfo{ Contracts::OrderRejectInfo::Code::InvalidQuantity, "invalid quantity" };
        return false;
    }

    if (request.kind != Contracts::OrderCommandKind::PerpClosePosition &&
        !validate_filters(step_state, request, quantity, reject)) {
        return false;
    }
    if ((request.kind == Contracts::OrderCommandKind::SpotLimit ||
            request.kind == Contracts::OrderCommandKind::PerpLimit) &&
        request.time_in_force == QTrading::Dto::Trading::TimeInForce::GTX &&
        is_immediately_marketable_post_only(step_state, request)) {
        reject = Contracts::OrderRejectInfo{
            Contracts::OrderRejectInfo::Code::PostOnlyWouldTake,
            "post-only order would take liquidity" };
        return false;
    }
    if (!validate_perp_protection_constraints(runtime_state, step_state, request, reject)) {
        return false;
    }
    Account::SelfTradePreventionMode effective_stp_mode = Account::SelfTradePreventionMode::None;
    if (!resolve_effective_stp_mode(step_state, request, effective_stp_mode, reject)) {
        return false;
    }

    if (request.instrument_type == QTrading::Dto::Trading::InstrumentType::Spot) {
        if (request.reduce_only) {
            reject = Contracts::OrderRejectInfo{
                Contracts::OrderRejectInfo::Code::SpotReduceOnlyUnsupported,
                "spot reduce_only is unsupported" };
            return false;
        }
        if (request.side == QTrading::Dto::Trading::OrderSide::Sell) {
            const double inventory = sum_spot_inventory(runtime_state, request.symbol);
            const double reserved_sell = sum_spot_sell_reservations(runtime_state, request.symbol);
            const double sellable = std::max(0.0, inventory - reserved_sell);
            if (sellable <= kEpsilon) {
                reject = Contracts::OrderRejectInfo{
                    Contracts::OrderRejectInfo::Code::SpotNoInventory,
                    "no spot inventory" };
                return false;
            }
            if (quantity > sellable + kEpsilon) {
                reject = Contracts::OrderRejectInfo{
                    Contracts::OrderRejectInfo::Code::SpotQuantityExceedsInventory,
                    "spot sell quantity exceeds inventory" };
                return false;
            }
        }
        else {
            const double ref_price = request.price > 0.0
                ? request.price
                : current_reference_price(step_state, request.symbol);
            if (ref_price <= 0.0) {
                reject = Contracts::OrderRejectInfo{
                    Contracts::OrderRejectInfo::Code::NotionalNoReferencePrice,
                    "missing reference price for spot buy" };
                return false;
            }
            const double notional = quantity * ref_price;
            const double needed = notional * spot_buy_reservation_multiplier(runtime_state);
            const double available = account.get_spot_cash_balance() - runtime_state.spot_open_order_initial_margin;
            if (needed > available + kEpsilon) {
                reject = Contracts::OrderRejectInfo{
                    Contracts::OrderRejectInfo::Code::SpotInsufficientCash,
                    "insufficient spot cash" };
                return false;
            }
        }
    }
    if (request.instrument_type == QTrading::Dto::Trading::InstrumentType::Perp) {
        if (request.kind == Contracts::OrderCommandKind::PerpClosePosition) {
            if (!request.close_position || request.reduce_only || request.quantity > kEpsilon) {
                reject = Contracts::OrderRejectInfo{
                    Contracts::OrderRejectInfo::Code::ClosePositionInvalidParameters,
                    "invalid closePosition parameter combination" };
                return false;
            }
            if (!runtime_state.hedge_mode &&
                request.position_side != QTrading::Dto::Trading::PositionSide::Both) {
                reject = Contracts::OrderRejectInfo{
                    Contracts::OrderRejectInfo::Code::ClosePositionInvalidParameters,
                    "one-way closePosition requires BOTH position side" };
                return false;
            }
            if (runtime_state.hedge_mode &&
                request.position_side == QTrading::Dto::Trading::PositionSide::Both) {
                reject = Contracts::OrderRejectInfo{
                    Contracts::OrderRejectInfo::Code::HedgeModePositionSideRequired,
                    "hedge mode requires explicit position side for closePosition" };
                return false;
            }
        }

        if (runtime_state.hedge_mode) {
            if (request.kind != Contracts::OrderCommandKind::PerpClosePosition &&
                request.position_side == QTrading::Dto::Trading::PositionSide::Both) {
                reject = Contracts::OrderRejectInfo{
                    Contracts::OrderRejectInfo::Code::HedgeModePositionSideRequired,
                    "hedge mode requires explicit position side" };
                return false;
            }
            if (request.reduce_only && !request.close_position && runtime_state.strict_binance_mode) {
                reject = Contracts::OrderRejectInfo{
                    Contracts::OrderRejectInfo::Code::StrictHedgeReduceOnlyDisabled,
                    "strict hedge reduce_only is disabled" };
                return false;
            }
            if (request.reduce_only || request.close_position) {
                const bool target_is_long =
                    request.position_side == QTrading::Dto::Trading::PositionSide::Long;
                if (!request_closes_target(request, target_is_long)) {
                    reject = Contracts::OrderRejectInfo{
                        Contracts::OrderRejectInfo::Code::ReduceOnlyNoReduciblePosition,
                        "hedge mode order side does not match target position side" };
                    return false;
                }
                const auto* position = find_perp_position(runtime_state, request.symbol, target_is_long);
                if (!position || position->quantity <= kEpsilon) {
                    reject = Contracts::OrderRejectInfo{
                        Contracts::OrderRejectInfo::Code::ReduceOnlyNoReduciblePosition,
                        "no reducible perp position" };
                    return false;
                }
                if (request.kind == Contracts::OrderCommandKind::PerpClosePosition) {
                    quantity = position->quantity;
                }
                else if (quantity > position->quantity + kEpsilon) {
                    quantity = position->quantity;
                }
            }
            else {
                const bool target_is_long =
                    request.position_side == QTrading::Dto::Trading::PositionSide::Long;
                if (!request_opens_target(request, target_is_long)) {
                    reject = Contracts::OrderRejectInfo{
                        Contracts::OrderRejectInfo::Code::HedgeModePositionSideRequired,
                        "hedge mode order side does not match target position side" };
                    return false;
                }
            }
        }
        else if (request.reduce_only || request.close_position) {
            const double reducible = reducible_perp_quantity(runtime_state, request);
            if (reducible <= kEpsilon) {
                reject = Contracts::OrderRejectInfo{
                    Contracts::OrderRejectInfo::Code::ReduceOnlyNoReduciblePosition,
                    "no reducible perp position" };
                return false;
            }
            if (request.kind == Contracts::OrderCommandKind::PerpClosePosition) {
                quantity = reducible;
            }
            else if (quantity > reducible + kEpsilon) {
                quantity = reducible;
            }
        }
    }
    if (effective_stp_mode != Account::SelfTradePreventionMode::None) {
        if (effective_stp_mode == Account::SelfTradePreventionMode::ExpireTaker) {
            const bool has_conflict = std::any_of(
                runtime_state.orders.begin(),
                runtime_state.orders.end(),
                [&](const QTrading::dto::Order& order) {
                    return is_self_trade_conflict(order, request);
                });
            if (has_conflict) {
                reject = Contracts::OrderRejectInfo{
                    Contracts::OrderRejectInfo::Code::StpExpiredTaker,
                    "self trade prevention rejected incoming order" };
                return false;
            }
        }
        else {
            const size_t removed = remove_orders_if(
                runtime_state,
                step_state,
                [&](const QTrading::dto::Order& order) {
                    return is_self_trade_conflict(order, request);
                });
            if (removed > 0) {
                ++step_state.account_state_version;
                if (effective_stp_mode == Account::SelfTradePreventionMode::ExpireBoth) {
                    reject = Contracts::OrderRejectInfo{
                        Contracts::OrderRejectInfo::Code::StpExpiredBoth,
                        "self trade prevention canceled resting order and rejected incoming order" };
                    return false;
                }
            }
        }
    }
    if (has_duplicate_client_order_id(runtime_state, request)) {
        reject = Contracts::OrderRejectInfo{
            Contracts::OrderRejectInfo::Code::DuplicateClientOrderId,
            "duplicate client order id" };
        return false;
    }
    if (request.instrument_type == QTrading::Dto::Trading::InstrumentType::Perp &&
        !request.reduce_only &&
        !request.close_position) {
        const auto perp = account.get_perp_balance();
        const double available = std::max(0.0, perp.AvailableBalance);
        const double required = estimate_added_perp_open_order_margin(
            runtime_state,
            step_state,
            request,
            quantity);
        if (required > available + kEpsilon) {
            reject = Contracts::OrderRejectInfo{
                Contracts::OrderRejectInfo::Code::PerpInsufficientMargin,
                "insufficient perp margin" };
            return false;
        }
    }

    if (request.instrument_type == QTrading::Dto::Trading::InstrumentType::Perp &&
        !request.reduce_only &&
        !request.close_position &&
        runtime_state.simulation_config.simulator_risk_overlay_enabled &&
        runtime_state.simulation_config.basis_stress_blocks_opening_orders &&
        runtime_state.simulation_config.basis_stress_bps > 0.0) {
        const auto basis_bps = current_basis_bps(runtime_state, step_state, request.symbol);
        if (basis_bps.has_value() && *basis_bps >= runtime_state.simulation_config.basis_stress_bps) {
            ++runtime_state.basis_stress_blocked_orders_total;
            reject = Contracts::OrderRejectInfo{
                Contracts::OrderRejectInfo::Code::Unknown,
                "basis stress blocks opening perp order" };
            return false;
        }
    }

    QTrading::dto::Order order{};
    order.id = static_cast<int>(runtime_state.next_order_id++);
    order.symbol = request.symbol;
    order.quantity = quantity;
    order.price = request.price;
    order.side = request.side;
    order.position_side = request.position_side;
    order.reduce_only = request.reduce_only;
    order.instrument_type = request.instrument_type;
    order.time_in_force = request.time_in_force;
    order.first_matching_step = request.first_matching_step;
    order.client_order_id = request.client_order_id;
    order.stp_mode = static_cast<int>(effective_stp_mode);
    order.close_position = request.close_position;
    order.quote_order_qty = request.quote_order_qty;
    const auto added_symbol_id = try_resolve_order_symbol_id(step_state, order.symbol);
    runtime_state.orders.emplace_back(std::move(order));
    ++runtime_state.orders_version;
    if (added_symbol_id.has_value()) {
        runtime_state.order_symbol_id_by_order_id[runtime_state.orders.back().id] = *added_symbol_id;
        runtime_state.order_symbol_id_by_slot.push_back(*added_symbol_id);
        refresh_symbol_reservations(runtime_state, step_state, std::vector<size_t>{ *added_symbol_id });
    }
    else {
        runtime_state.order_symbol_id_by_slot.push_back(std::numeric_limits<size_t>::max());
        sync_open_order_margins(runtime_state, step_state);
    }
    ++step_state.account_state_version;
    return true;
}

void OrderEntryService::SyncOpenOrderMargins(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state)
{
    sync_open_order_margins(runtime_state, step_state);
}

bool OrderEntryService::CancelOrderById(
    State::BinanceExchangeRuntimeState& runtime_state,
    State::StepKernelState& step_state,
    int order_id)
{
    if (!remove_order_by_id(runtime_state, step_state, order_id)) {
        return false;
    }
    ++step_state.account_state_version;
    return true;
}

void OrderEntryService::CancelOpenOrders(
    State::BinanceExchangeRuntimeState& runtime_state,
    State::StepKernelState& step_state,
    QTrading::Dto::Trading::InstrumentType instrument_type,
    const std::string& symbol)
{
    const size_t removed = remove_orders_if(
        runtime_state,
        step_state,
        [&](const QTrading::dto::Order& order) {
            return order.symbol == symbol && order.instrument_type == instrument_type;
        });
    if (removed > 0) {
        ++step_state.account_state_version;
    }
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
