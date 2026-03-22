#include "Exchanges/BinanceSimulator/Domain/OrderEntryService.hpp"

#include <algorithm>

#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"
#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Domain {

namespace {

bool symbol_exists(
    const State::StepKernelState& step_state,
    const std::string& symbol)
{
    return std::find(step_state.symbols.begin(), step_state.symbols.end(), symbol) != step_state.symbols.end();
}

double effective_quantity(const Contracts::OrderCommandRequest& request)
{
    if (request.kind == Contracts::OrderCommandKind::SpotMarketQuote) {
        return request.quote_order_qty;
    }
    if (request.kind == Contracts::OrderCommandKind::PerpClosePosition) {
        return 0.0;
    }
    return request.quantity;
}

} // namespace

bool OrderEntryService::Execute(
    State::BinanceExchangeRuntimeState& runtime_state,
    const State::StepKernelState& step_state,
    const Contracts::OrderCommandRequest& request,
    std::optional<Contracts::OrderRejectInfo>& reject)
{
    reject.reset();

    if (!symbol_exists(step_state, request.symbol)) {
        reject = Contracts::OrderRejectInfo{ Contracts::OrderRejectInfo::Code::UnknownSymbol, "unknown symbol" };
        return false;
    }

    if (request.kind != Contracts::OrderCommandKind::PerpClosePosition &&
        effective_quantity(request) <= 0.0) {
        reject = Contracts::OrderRejectInfo{ Contracts::OrderRejectInfo::Code::InvalidQuantity, "invalid quantity" };
        return false;
    }

    if (request.instrument_type == QTrading::Dto::Trading::InstrumentType::Spot &&
        request.side == QTrading::Dto::Trading::OrderSide::Sell) {
        reject = Contracts::OrderRejectInfo{ Contracts::OrderRejectInfo::Code::SpotNoInventory, "no spot inventory" };
        return false;
    }

    QTrading::dto::Order order{};
    order.id = static_cast<int>(runtime_state.next_order_id++);
    order.symbol = request.symbol;
    order.quantity = request.quantity;
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
    return true;
}

void OrderEntryService::CancelOpenOrders(
    State::BinanceExchangeRuntimeState& runtime_state,
    QTrading::Dto::Trading::InstrumentType instrument_type,
    const std::string& symbol)
{
    auto& orders = runtime_state.orders;
    orders.erase(
        std::remove_if(orders.begin(), orders.end(), [&](const QTrading::dto::Order& order) {
            return order.symbol == symbol && order.instrument_type == instrument_type;
        }),
        orders.end());
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Domain
