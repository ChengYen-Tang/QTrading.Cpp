#include "Strategy/FundingCarryStrategyGateway.hpp"

#include "Dto/Trading/Side.hpp"
#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"

#include <algorithm>
#include <optional>
#include <string_view>
#include <unordered_map>

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

bool EndsWith(const std::string& value, std::string_view suffix)
{
    return value.size() >= suffix.size() &&
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string PairRootSymbol(const std::string& symbol)
{
    if (EndsWith(symbol, "_SPOT")) {
        return symbol.substr(0, symbol.size() - 5);
    }
    if (EndsWith(symbol, "_PERP")) {
        return symbol.substr(0, symbol.size() - 5);
    }
    return symbol;
}

bool SubmitSingleOrder(
    const std::shared_ptr<QTrading::Infra::Exchanges::BinanceSim::BinanceExchange>& exchange,
    const std::unordered_map<std::string, QTrading::Dto::Trading::InstrumentType>& instrument_types,
    const QTrading::Execution::ExecutionOrder& order)
{
    const double price =
        (order.type == QTrading::Execution::OrderType::Limit) ? order.price : 0.0;
    const auto type = ResolveInstrumentType(instrument_types, order.symbol);

    if (type.has_value() && *type == QTrading::Dto::Trading::InstrumentType::Spot) {
        return exchange->spot.place_order(
            order.symbol,
            order.qty,
            price,
            ToOrderSide(order.action),
            false);
    }

    return exchange->perp.place_order(
        order.symbol,
        order.qty,
        price,
        ToOrderSide(order.action),
        QTrading::Dto::Trading::PositionSide::Both,
        order.reduce_only);
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
    struct PairBatch {
        std::vector<const QTrading::Execution::ExecutionOrder*> spot_orders;
        std::vector<const QTrading::Execution::ExecutionOrder*> perp_orders;
        std::vector<const QTrading::Execution::ExecutionOrder*> other_orders;
    };

    std::vector<std::string> pair_order_sequence;
    pair_order_sequence.reserve(orders.size());
    std::unordered_map<std::string, PairBatch> batches;
    batches.reserve(orders.size());

    for (const auto& order : orders) {
        const std::string root = PairRootSymbol(order.symbol);
        auto [it, inserted] = batches.try_emplace(root);
        if (inserted) {
            pair_order_sequence.push_back(root);
        }

        const auto type = ResolveInstrumentType(instrument_types_, order.symbol);
        if (type.has_value() && *type == QTrading::Dto::Trading::InstrumentType::Spot) {
            it->second.spot_orders.push_back(&order);
        }
        else if (type.has_value() && *type == QTrading::Dto::Trading::InstrumentType::Perp) {
            it->second.perp_orders.push_back(&order);
        }
        else {
            it->second.other_orders.push_back(&order);
        }
    }

    for (const auto& root : pair_order_sequence) {
        const auto batch_it = batches.find(root);
        if (batch_it == batches.end()) {
            continue;
        }
        const auto& batch = batch_it->second;

        for (const auto* order : batch.other_orders) {
            (void)SubmitSingleOrder(exchange_, instrument_types_, *order);
        }

        if (batch.spot_orders.empty() || batch.perp_orders.empty()) {
            for (const auto* order : batch.spot_orders) {
                (void)SubmitSingleOrder(exchange_, instrument_types_, *order);
            }
            for (const auto* order : batch.perp_orders) {
                (void)SubmitSingleOrder(exchange_, instrument_types_, *order);
            }
            continue;
        }

        bool any_spot_submitted = false;
        for (const auto* order : batch.spot_orders) {
            any_spot_submitted = SubmitSingleOrder(exchange_, instrument_types_, *order) || any_spot_submitted;
        }
        if (!any_spot_submitted) {
            continue;
        }
        for (const auto* order : batch.perp_orders) {
            (void)SubmitSingleOrder(exchange_, instrument_types_, *order);
        }
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
