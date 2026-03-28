#include "Exchanges/BinanceSimulator/Domain/OrderEntryService.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Exchanges/BinanceSimulator/Account/Account.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"
#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

namespace {

constexpr double kFeeRate = 0.001;
constexpr double kEpsilon = 1e-12;

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
        const double ref_price = request.price > 0.0
            ? request.price
            : current_reference_price(step_state, request.symbol);
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
        reserved += quote * (1.0 + kFeeRate);
    }
    return reserved;
}

bool has_duplicate_client_order_id(const State::BinanceExchangeRuntimeState& runtime_state, const std::string& client_id)
{
    if (client_id.empty()) {
        return false;
    }
    for (const auto& order : runtime_state.orders) {
        if (order.client_order_id == client_id) {
            return true;
        }
    }
    return false;
}

const QTrading::dto::Position* find_perp_position(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const std::string& symbol)
{
    for (const auto& position : runtime_state.positions) {
        if (position.instrument_type == QTrading::Dto::Trading::InstrumentType::Perp &&
            position.symbol == symbol) {
            return &position;
        }
    }
    return nullptr;
}

double reducible_perp_quantity(
    const State::BinanceExchangeRuntimeState& runtime_state,
    const Contracts::OrderCommandRequest& request)
{
    const auto* position = find_perp_position(runtime_state, request.symbol);
    if (!position || position->quantity <= kEpsilon) {
        return 0.0;
    }

    if (request.position_side == QTrading::Dto::Trading::PositionSide::Long && !position->is_long) {
        return 0.0;
    }
    if (request.position_side == QTrading::Dto::Trading::PositionSide::Short && position->is_long) {
        return 0.0;
    }

    const bool order_is_buy = request.side == QTrading::Dto::Trading::OrderSide::Buy;
    const bool reduces = (position->is_long && !order_is_buy) || (!position->is_long && order_is_buy);
    if (!reduces) {
        return 0.0;
    }
    return position->quantity;
}

} // namespace

bool OrderEntryService::Execute(
    State::BinanceExchangeRuntimeState& runtime_state,
    const Account& account,
    State::StepKernelState& step_state,
    const Contracts::OrderCommandRequest& request,
    std::optional<Contracts::OrderRejectInfo>& reject)
{
    reject.reset();

    if (!symbol_exists(step_state, request.symbol)) {
        reject = Contracts::OrderRejectInfo{ Contracts::OrderRejectInfo::Code::UnknownSymbol, "unknown symbol" };
        return false;
    }

    if (has_duplicate_client_order_id(runtime_state, request.client_order_id)) {
        reject = Contracts::OrderRejectInfo{
            Contracts::OrderRejectInfo::Code::DuplicateClientOrderId,
            "duplicate client order id" };
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
            const double needed = notional * (1.0 + kFeeRate);
            const double reserved_buy = sum_spot_buy_reservations(runtime_state);
            const double available = account.get_spot_balance().AvailableBalance - reserved_buy;
            if (available + kEpsilon < needed) {
                reject = Contracts::OrderRejectInfo{
                    Contracts::OrderRejectInfo::Code::SpotInsufficientCash,
                    "insufficient spot cash" };
                return false;
            }
        }
    }
    if (request.instrument_type == QTrading::Dto::Trading::InstrumentType::Perp &&
        (request.reduce_only || request.close_position)) {
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
        ++step_state.account_state_version;
    }
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
