#include "Exchanges/BinanceSimulator/Adapters/AccountFacadeAdapter.hpp"

#include "Exchanges/BinanceSimulator/Account/Account.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Adapters {
namespace {

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
    const double available = account.get_spot_cash_balance() - runtime_state.spot_open_order_initial_margin;
    if (amount > available + 1e-12) {
        return false;
    }
    return account.transfer_spot_to_perp(amount);
}

bool AccountFacadeAdapter::TransferPerpToSpot(
    Account& account,
    const State::BinanceExchangeRuntimeState& runtime_state,
    double amount)
{
    const double available = account.get_wallet_balance() - runtime_state.perp_open_order_initial_margin;
    if (amount <= 0.0 || amount > available + 1e-12) {
        return false;
    }
    return account.transfer_perp_to_spot(amount);
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Adapters
