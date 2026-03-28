#include "Exchanges/BinanceSimulator/Domain/OrderEntryService.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_map>

#include "Exchanges/BinanceSimulator/Account/Account.hpp"
#include "Exchanges/BinanceSimulator/Account/Config.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"
#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

namespace {

constexpr double kPerpMarketReservationBuffer = 1.001;
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
    const size_t idx = cursor == 0 ? 0 : std::min(cursor - 1, count - 1);
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
    const size_t idx = cursor == 0 ? 0 : std::min(cursor - 1, count - 1);
    return mark_data.get_kline(idx).ClosePrice;
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

double sum_spot_buy_reservations(const State::BinanceExchangeRuntimeState& runtime_state)
{
    double reserved = 0.0;
    for (const auto& order : runtime_state.orders) {
        if (order.instrument_type != QTrading::Dto::Trading::InstrumentType::Spot ||
            order.side != QTrading::Dto::Trading::OrderSide::Buy) {
            continue;
        }
        const double price = order.price > 0.0 ? order.price : order.quote_order_qty > 0.0 ? 1.0 : 0.0;
        const double quote = order.quote_order_qty > 0.0 ? order.quote_order_qty : order.quantity * price;
        reserved += quote * spot_buy_reservation_multiplier(runtime_state);
    }
    return reserved;
}

double sum_perp_open_order_reservations(const State::BinanceExchangeRuntimeState& runtime_state)
{
    auto resolve_perp_reference_price = [&](const QTrading::dto::Order& order) -> double {
        if (order.price > 0.0) {
            return order.price;
        }
        for (const auto& snapshot : runtime_state.last_status_snapshot.prices) {
            if (snapshot.symbol != order.symbol) {
                continue;
            }
            if (snapshot.has_mark_price && snapshot.mark_price > 0.0) {
                return snapshot.mark_price * kPerpMarketReservationBuffer;
            }
            if (snapshot.has_trade_price && snapshot.trade_price > 0.0) {
                return snapshot.trade_price * kPerpMarketReservationBuffer;
            }
            if (snapshot.has_price && snapshot.price > 0.0) {
                return snapshot.price * kPerpMarketReservationBuffer;
            }
        }
        return 0.0;
    };

    auto one_way_net_position_qty = [&](const std::string& symbol) -> double {
        double net = 0.0;
        for (const auto& position : runtime_state.positions) {
            if (position.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp ||
                position.symbol != symbol) {
                continue;
            }
            net += position.is_long ? position.quantity : -position.quantity;
        }
        return net;
    };

    double reserved = 0.0;
    std::unordered_map<std::string, double> effective_net_by_symbol{};
    for (const auto& order : runtime_state.orders) {
        if (order.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp ||
            order.reduce_only ||
            order.close_position) {
            continue;
        }
        const double price = resolve_perp_reference_price(order);
        if (price <= 0.0) {
            continue;
        }

        double opening_qty = std::max(0.0, order.quantity);
        if (!runtime_state.hedge_mode && opening_qty > kEpsilon) {
            auto net_it = effective_net_by_symbol.find(order.symbol);
            if (net_it == effective_net_by_symbol.end()) {
                net_it = effective_net_by_symbol.emplace(
                    order.symbol,
                    one_way_net_position_qty(order.symbol)).first;
            }
            const double net_position = net_it->second;
            const double signed_order = order.side == QTrading::Dto::Trading::OrderSide::Buy
                ? opening_qty
                : -opening_qty;
            if (std::abs(net_position) > kEpsilon && net_position * signed_order < 0.0) {
                const double closing_qty = std::min(std::abs(net_position), std::abs(signed_order));
                opening_qty = std::max(0.0, opening_qty - closing_qty);
            }
            net_it->second = net_position + signed_order;
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

void sync_open_order_margins(State::BinanceExchangeRuntimeState& runtime_state);

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
    int order_id)
{
    auto& orders = runtime_state.orders;
    const size_t before = orders.size();
    orders.erase(
        std::remove_if(orders.begin(), orders.end(), [&](const QTrading::dto::Order& order) {
            return order.id == order_id;
        }),
        orders.end());
    if (orders.size() != before) {
        sync_open_order_margins(runtime_state);
        return true;
    }
    return false;
}
void sync_open_order_margins(State::BinanceExchangeRuntimeState& runtime_state)
{
    runtime_state.spot_open_order_initial_margin = sum_spot_buy_reservations(runtime_state);
    runtime_state.perp_open_order_initial_margin = sum_perp_open_order_reservations(runtime_state);
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
    const std::string& symbol)
{
    for (const auto& snap : runtime_state.last_status_snapshot.prices) {
        if (snap.symbol != symbol ||
            !snap.has_mark_price ||
            !snap.has_index_price ||
            std::abs(snap.index_price) <= kEpsilon) {
            continue;
        }
        return std::abs((snap.mark_price - snap.index_price) / snap.index_price) * 10000.0;
    }
    return std::nullopt;
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

    if (request.instrument_type == QTrading::Dto::Trading::InstrumentType::Spot) {
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
            if (request.reduce_only && inventory <= kEpsilon) {
                reject = Contracts::OrderRejectInfo{
                    Contracts::OrderRejectInfo::Code::ReduceOnlyNoReduciblePosition,
                    "no reducible spot position" };
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
    if (request.stp_mode != static_cast<int>(Account::SelfTradePreventionMode::None)) {
        const auto stp_mode = static_cast<Account::SelfTradePreventionMode>(request.stp_mode);
        const bool has_conflict = std::any_of(
            runtime_state.orders.begin(),
            runtime_state.orders.end(),
            [&](const QTrading::dto::Order& order) {
                return is_self_trade_conflict(order, request);
            });
        if (has_conflict) {
            if (stp_mode == Account::SelfTradePreventionMode::ExpireTaker) {
                reject = Contracts::OrderRejectInfo{
                    Contracts::OrderRejectInfo::Code::StpExpiredTaker,
                    "self trade prevention rejected incoming order" };
                return false;
            }

            const size_t before = runtime_state.orders.size();
            runtime_state.orders.erase(
                std::remove_if(runtime_state.orders.begin(), runtime_state.orders.end(),
                    [&](const QTrading::dto::Order& order) {
                        return is_self_trade_conflict(order, request);
                    }),
                runtime_state.orders.end());
            if (runtime_state.orders.size() != before) {
                sync_open_order_margins(runtime_state);
                ++step_state.account_state_version;
            }
            if (stp_mode == Account::SelfTradePreventionMode::ExpireBoth) {
                reject = Contracts::OrderRejectInfo{
                    Contracts::OrderRejectInfo::Code::StpExpiredBoth,
                    "self trade prevention canceled resting order and rejected incoming order" };
                return false;
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
        !request.close_position &&
        runtime_state.simulation_config.simulator_risk_overlay_enabled &&
        runtime_state.simulation_config.basis_stress_blocks_opening_orders &&
        runtime_state.simulation_config.basis_stress_bps > 0.0) {
        const auto basis_bps = current_basis_bps(runtime_state, request.symbol);
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
    order.client_order_id = request.client_order_id;
    order.stp_mode = request.stp_mode;
    order.close_position = request.close_position;
    order.quote_order_qty = request.quote_order_qty;
    runtime_state.orders.emplace_back(std::move(order));
    sync_open_order_margins(runtime_state);
    ++step_state.account_state_version;
    return true;
}

void OrderEntryService::SyncOpenOrderMargins(State::BinanceExchangeRuntimeState& runtime_state)
{
    sync_open_order_margins(runtime_state);
}

bool OrderEntryService::CancelOrderById(
    State::BinanceExchangeRuntimeState& runtime_state,
    State::StepKernelState& step_state,
    int order_id)
{
    if (!remove_order_by_id(runtime_state, order_id)) {
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
    auto& orders = runtime_state.orders;
    const size_t before = orders.size();
    orders.erase(
        std::remove_if(orders.begin(), orders.end(), [&](const QTrading::dto::Order& order) {
            return order.symbol == symbol && order.instrument_type == instrument_type;
        }),
        orders.end());
    if (orders.size() != before) {
        SyncOpenOrderMargins(runtime_state);
        ++step_state.account_state_version;
    }
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
