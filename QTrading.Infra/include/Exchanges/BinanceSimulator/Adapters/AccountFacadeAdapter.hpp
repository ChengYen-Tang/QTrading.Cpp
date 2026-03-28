#pragma once

#include "Dto/Account/BalanceSnapshot.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {
class Account;
}
namespace QTrading::Infra::Exchanges::BinanceSim::State {
struct BinanceExchangeRuntimeState;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Adapters {

/// Thin adapter that maps facade account API calls to Account methods.
/// Exists to keep facade/application layers decoupled from account internals.
class AccountFacadeAdapter final {
public:
    static QTrading::Dto::Account::BalanceSnapshot GetSpotBalance(const Account& account);
    static QTrading::Dto::Account::BalanceSnapshot GetPerpBalance(const Account& account);
    static double GetTotalCashBalance(const Account& account);
    static bool TransferSpotToPerp(
        Account& account,
        const State::BinanceExchangeRuntimeState& runtime_state,
        double amount);
    static bool TransferPerpToSpot(
        Account& account,
        const State::BinanceExchangeRuntimeState& runtime_state,
        double amount);
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Adapters
