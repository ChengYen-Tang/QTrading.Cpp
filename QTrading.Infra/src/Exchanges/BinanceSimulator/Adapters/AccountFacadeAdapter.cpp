#include "Exchanges/BinanceSimulator/Adapters/AccountFacadeAdapter.hpp"

#include "Exchanges/BinanceSimulator/Account/Account.hpp"
#include "Exchanges/BinanceSimulator/Support/BinanceExchangeSkeletonSupport.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Adapters {

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

bool AccountFacadeAdapter::TransferSpotToPerp(Account&, double)
{
    // Transfer semantics are intentionally deferred to later rebuild phases.
    Support::ThrowNotImplemented("AccountFacadeAdapter::TransferSpotToPerp");
}

bool AccountFacadeAdapter::TransferPerpToSpot(Account&, double)
{
    Support::ThrowNotImplemented("AccountFacadeAdapter::TransferPerpToSpot");
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Adapters
