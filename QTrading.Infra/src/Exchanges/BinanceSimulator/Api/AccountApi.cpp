#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/Adapters/AccountFacadeAdapter.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Api {

QTrading::Dto::Account::BalanceSnapshot AccountApi::get_spot_balance() const
{
    return Adapters::AccountFacadeAdapter::GetSpotBalance(owner_.account_state());
}

QTrading::Dto::Account::BalanceSnapshot AccountApi::get_perp_balance() const
{
    return Adapters::AccountFacadeAdapter::GetPerpBalance(owner_.account_state());
}

double AccountApi::get_total_cash_balance() const
{
    return Adapters::AccountFacadeAdapter::GetTotalCashBalance(owner_.account_state());
}

bool AccountApi::transfer_spot_to_perp(double amount)
{
    return Adapters::AccountFacadeAdapter::TransferSpotToPerp(owner_.account_state(), amount);
}

bool AccountApi::transfer_perp_to_spot(double amount)
{
    return Adapters::AccountFacadeAdapter::TransferPerpToSpot(owner_.account_state(), amount);
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Api
