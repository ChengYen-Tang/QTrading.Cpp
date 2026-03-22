#pragma once

#include "Dto/Account/BalanceSnapshot.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {
class BinanceExchange;
}

namespace QTrading::Infra::Exchanges::BinanceSim::Api {

/// Account read/transfer facade exposed by BinanceExchange.
/// Delegates to adapter layer to isolate facade from account engine details.
class AccountApi {
public:
    explicit AccountApi(BinanceExchange& owner) noexcept : owner_(owner) {}

    /// Returns current spot balance snapshot.
    QTrading::Dto::Account::BalanceSnapshot get_spot_balance() const;
    /// Returns current perp balance snapshot.
    QTrading::Dto::Account::BalanceSnapshot get_perp_balance() const;
    /// Returns aggregate cash balance across ledgers.
    double get_total_cash_balance() const;
    /// Transfers cash from spot ledger to perp ledger.
    bool transfer_spot_to_perp(double amount);
    /// Transfers cash from perp ledger to spot ledger.
    bool transfer_perp_to_spot(double amount);

private:
    /// Non-owning facade reference.
    BinanceExchange& owner_;
};

} // namespace QTrading::Infra::Exchanges::BinanceSim::Api
