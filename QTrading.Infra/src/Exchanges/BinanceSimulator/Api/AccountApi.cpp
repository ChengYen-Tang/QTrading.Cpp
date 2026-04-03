#include "Exchanges/BinanceSimulator/BinanceExchange.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Api {

QTrading::Dto::Account::BalanceSnapshot AccountApi::get_spot_balance() const
{
    return owner_.account_state().get_spot_balance();
}

QTrading::Dto::Account::BalanceSnapshot AccountApi::get_perp_balance() const
{
    return owner_.account_state().get_perp_balance();
}

double AccountApi::get_total_cash_balance() const
{
    return owner_.account_state().get_total_cash_balance();
}

bool AccountApi::transfer_spot_to_perp(double amount)
{
    if (amount <= 0.0) {
        return false;
    }

    const double available =
        owner_.account_state().get_spot_cash_balance() - owner_.runtime_state_->spot_open_order_initial_margin;
    if (amount > available + 1e-12) {
        return false;
    }

    return owner_.account_state().transfer_spot_to_perp(amount);
}

bool AccountApi::transfer_perp_to_spot(double amount)
{
    const double available =
        owner_.account_state().get_wallet_balance() - owner_.runtime_state_->perp_open_order_initial_margin;
    if (amount <= 0.0 || amount > available + 1e-12) {
        return false;
    }

    return owner_.account_state().transfer_perp_to_spot(amount);
}

} // namespace QTrading::Infra::Exchanges::BinanceSim::Api
