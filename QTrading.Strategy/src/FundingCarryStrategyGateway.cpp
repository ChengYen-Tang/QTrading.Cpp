#include "Strategy/FundingCarryStrategyGateway.hpp"

#include "Dto/Trading/Side.hpp"
#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

#include <optional>

namespace {

QTrading::Dto::Trading::OrderSide ToOrderSide(QTrading::Execution::OrderAction action)
{
    return (action == QTrading::Execution::OrderAction::Buy)
        ? QTrading::Dto::Trading::OrderSide::Buy
        : QTrading::Dto::Trading::OrderSide::Sell;
}

std::optional<QTrading::Dto::Trading::InstrumentType> ResolveInstrumentType(
    const std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType>& explicit_types,
    const std::string& symbol)
{
    const auto it = explicit_types.find(symbol);
    if (it != explicit_types.end()) {
        return it->second;
    }
    return std::nullopt;
}

} // namespace

namespace QTrading::Strategy {

FundingCarryStrategyGateway::FundingCarryStrategyGateway(
    std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange> exchange,
    std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType> instrument_types)
    : exchange_(std::move(exchange))
    , instrument_types_(std::move(instrument_types))
{
}

QTrading::Risk::AccountState FundingCarryStrategyGateway::BuildAccountState() const
{
    QTrading::Risk::AccountState account{};
    account.positions = exchange_->get_all_positions();
    account.open_orders = exchange_->get_all_open_orders();
    account.spot_balance = exchange_->account.get_spot_balance();
    account.perp_balance = exchange_->account.get_perp_balance();
    account.total_cash_balance = exchange_->account.get_total_cash_balance();
    return account;
}

void FundingCarryStrategyGateway::SubmitOrders(
    const std::vector<QTrading::Execution::ExecutionOrder>& orders)
{
    for (const auto& order : orders) {
        const double price =
            (order.type == QTrading::Execution::OrderType::Limit) ? order.price : 0.0;
        const auto type = ResolveInstrumentType(instrument_types_, order.symbol);

        if (type.has_value() && *type == QTrading::Dto::Trading::InstrumentType::Spot) {
            (void)exchange_->spot.place_order(
                order.symbol,
                order.qty,
                price,
                ToOrderSide(order.action),
                false);
            continue;
        }

        (void)exchange_->perp.place_order(
            order.symbol,
            order.qty,
            price,
            ToOrderSide(order.action),
            QTrading::Dto::Trading::PositionSide::Both,
            order.reduce_only);
    }
}

void FundingCarryStrategyGateway::ApplyMonitoringAlerts(
    const std::vector<QTrading::Monitoring::MonitoringAlert>& alerts)
{
    for (const auto& alert : alerts) {
        if (alert.action != "CANCEL_OPEN_ORDERS") {
            continue;
        }

        const auto type = ResolveInstrumentType(instrument_types_, alert.symbol);
        if (type.has_value() && *type == QTrading::Dto::Trading::InstrumentType::Spot) {
            exchange_->spot.cancel_open_orders(alert.symbol);
            continue;
        }
        exchange_->perp.cancel_open_orders(alert.symbol);
    }
}

} // namespace QTrading::Strategy
