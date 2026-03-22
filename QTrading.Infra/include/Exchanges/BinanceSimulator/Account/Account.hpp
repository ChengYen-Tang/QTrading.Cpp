#pragma once

#include <memory>
#include <string>

#include "Dto/Account/BalanceSnapshot.hpp"
#include "Exchanges/BinanceSimulator/Contracts/AccountInitConfig.hpp"
#include "Exchanges/BinanceSimulator/Contracts/OrderRejectInfo.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim {

/// Minimal account skeleton used by rebuilt facade phases.
/// Only read-path balances are implemented; trading mutations are deferred.
class Account {
public:
    using AccountInitConfig = Contracts::AccountInitConfig;

    /// Enumerates self-trade-prevention options preserved on API signatures.
    enum class SelfTradePreventionMode {
        None = 0,
        ExpireTaker = 1,
        ExpireMaker = 2,
        ExpireBoth = 3,
    };

    Account() = default;
    /// Initializes spot/perp balance ledgers from bootstrap config.
    explicit Account(const AccountInitConfig& init)
        : spot_balance_(make_balance_(init.spot_initial_cash)),
          perp_balance_(make_balance_(init.perp_initial_wallet)),
          total_cash_balance_(init.spot_initial_cash + init.perp_initial_wallet) {}

    /// Returns immutable spot balance snapshot.
    QTrading::Dto::Account::BalanceSnapshot get_spot_balance() const { return spot_balance_; }
    /// Returns immutable perp balance snapshot.
    QTrading::Dto::Account::BalanceSnapshot get_perp_balance() const { return perp_balance_; }
    /// Returns aggregate cash across ledgers.
    double get_total_cash_balance() const { return total_cash_balance_; }

private:
    static QTrading::Dto::Account::BalanceSnapshot make_balance_(double wallet)
    {
        QTrading::Dto::Account::BalanceSnapshot snapshot{};
        snapshot.WalletBalance = wallet;
        snapshot.MarginBalance = wallet;
        snapshot.AvailableBalance = wallet;
        snapshot.Equity = wallet;
        return snapshot;
    }

    QTrading::Dto::Account::BalanceSnapshot spot_balance_{ make_balance_(1'000'000.0) };
    QTrading::Dto::Account::BalanceSnapshot perp_balance_{};
    double total_cash_balance_{ 1'000'000.0 };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim

using Account = QTrading::Infra::Exchanges::BinanceSim::Account;
