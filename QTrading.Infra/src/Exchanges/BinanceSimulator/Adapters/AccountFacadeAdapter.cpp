#include "Exchanges/BinanceSimulator/Adapters/AccountFacadeAdapter.hpp"

#include "Exchanges/BinanceSimulator/Account/Account.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Adapters {
namespace {

constexpr double kSpotFeeRate = 0.001;

double sum_spot_buy_open_order_notional_reserved(
    const State::BinanceExchangeRuntimeState& runtime_state)
{
    double reserved = 0.0;
    for (const auto& order : runtime_state.orders) {
        if (order.instrument_type != QTrading::Dto::Trading::InstrumentType::Spot ||
            order.side != QTrading::Dto::Trading::OrderSide::Buy) {
            continue;
        }
        const double price = order.price > 0.0 ? order.price : order.quote_order_qty > 0.0 ? 1.0 : 0.0;
        const double quote = order.quote_order_qty > 0.0 ? order.quote_order_qty : order.quantity * price;
        reserved += quote * (1.0 + kSpotFeeRate);
    }
    return reserved;
}

} // namespace

QTrading::Dto::Account::BalanceSnapshot AccountFacadeAdapter::GetSpotBalance(const Account& account)
{
    return account.get_spot_balance();
}

QTrading::Dto::Account::BalanceSnapshot AccountFacadeAdapter::GetPerpBalance(const Account& account)
{
    return account.get_perp_balance();
}

double AccountFacadeAdapter::GetTotalCashBalance(const Account& account)
{
    return account.get_total_cash_balance();
}

bool AccountFacadeAdapter::TransferSpotToPerp(
    Account& account,
    const State::BinanceExchangeRuntimeState& runtime_state,
    double amount)
{
    if (amount <= 0.0) {
        return false;
    }
    const auto spot_balance = account.get_spot_balance();
    const double reserved = sum_spot_buy_open_order_notional_reserved(runtime_state);
    const double transferable = spot_balance.AvailableBalance - reserved;
    if (transferable + 1e-12 < amount) {
        return false;
    }
    return account.transfer_spot_to_perp(amount);
}

bool AccountFacadeAdapter::TransferPerpToSpot(
    Account& account,
    const State::BinanceExchangeRuntimeState&,
    double amount)
{
    return account.transfer_perp_to_spot(amount);
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Adapters
