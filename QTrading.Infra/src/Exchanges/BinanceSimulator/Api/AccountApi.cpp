#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Adapters/AccountFacadeAdapter.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {

QTrading::Dto::Account::BalanceSnapshot Api::AccountApi::get_spot_balance() const
{
    return Adapters::AccountFacadeAdapter::GetSpotBalance(owner_.account_state());
}

QTrading::Dto::Account::BalanceSnapshot Api::AccountApi::get_perp_balance() const
{
    return Adapters::AccountFacadeAdapter::GetPerpBalance(owner_.account_state());
}

double Api::AccountApi::get_total_cash_balance() const
{
    return Adapters::AccountFacadeAdapter::GetTotalCashBalance(owner_.account_state());
}

bool Api::AccountApi::transfer_spot_to_perp(double amount)
{
    return Adapters::AccountFacadeAdapter::TransferSpotToPerp(owner_.account_state(), amount);
}

bool Api::AccountApi::transfer_perp_to_spot(double amount)
{
    return Adapters::AccountFacadeAdapter::TransferPerpToSpot(owner_.account_state(), amount);
}

} // namespace QTrading::Infra::Exchanges::BinanceSim
