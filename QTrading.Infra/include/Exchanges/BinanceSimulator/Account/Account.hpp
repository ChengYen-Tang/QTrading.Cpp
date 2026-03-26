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
    /// Applies spot cash delta and keeps snapshot fields aligned.
    void apply_spot_cash_delta(double delta)
    {
        spot_balance_.WalletBalance += delta;
        sync_snapshot_(spot_balance_);
        sync_total_cash_();
    }
    /// Applies perp wallet delta and keeps snapshot fields aligned.
    void apply_perp_wallet_delta(double delta)
    {
        perp_balance_.WalletBalance += delta;
        sync_snapshot_(perp_balance_);
        sync_total_cash_();
    }
    /// Available-balance check for spot-side cash operations.
    bool can_debit_spot(double amount) const
    {
        return amount <= 0.0 || spot_balance_.AvailableBalance + 1e-12 >= amount;
    }
    /// Available-balance check for perp-side cash operations.
    bool can_debit_perp(double amount) const
    {
        return amount <= 0.0 || perp_balance_.AvailableBalance + 1e-12 >= amount;
    }
    /// Moves cash from spot to perp if available balance is sufficient.
    bool transfer_spot_to_perp(double amount)
    {
        if (amount <= 0.0 || !can_debit_spot(amount)) {
            return false;
        }
        apply_spot_cash_delta(-amount);
        apply_perp_wallet_delta(amount);
        return true;
    }
    /// Moves cash from perp to spot if available balance is sufficient.
    bool transfer_perp_to_spot(double amount)
    {
        if (amount <= 0.0 || !can_debit_perp(amount)) {
            return false;
        }
        apply_perp_wallet_delta(-amount);
        apply_spot_cash_delta(amount);
        return true;
    }

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
    static void sync_snapshot_(QTrading::Dto::Account::BalanceSnapshot& snapshot)
    {
        snapshot.MarginBalance = snapshot.WalletBalance;
        snapshot.AvailableBalance = snapshot.WalletBalance;
        snapshot.Equity = snapshot.WalletBalance;
    }
    void sync_total_cash_()
    {
        total_cash_balance_ = spot_balance_.WalletBalance + perp_balance_.WalletBalance;
    }

    QTrading::Dto::Account::BalanceSnapshot spot_balance_{ make_balance_(1'000'000.0) };
    QTrading::Dto::Account::BalanceSnapshot perp_balance_{};
    double total_cash_balance_{ 1'000'000.0 };
};

} // namespace QTrading::Infra::Exchanges::BinanceSim

using Account = QTrading::Infra::Exchanges::BinanceSim::Account;
