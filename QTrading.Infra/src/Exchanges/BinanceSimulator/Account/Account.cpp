#include "Exchanges/BinanceSimulator/Account/Account.hpp"

#include <algorithm>
#include <stdexcept>

namespace QTrading::Infra::Exchanges::BinanceSim {

Account::Account(double init_balance, int)
    : Account(build_init_from_balance_(init_balance)) {}

Account::Account(const AccountInitConfig& init)
    : spot_balance_(make_balance_(validate_non_negative_(init.spot_initial_cash, "spot_initial_cash"))),
      perp_balance_(make_balance_(validate_non_negative_(init.perp_initial_wallet, "perp_initial_wallet"))),
      total_cash_balance_(init.spot_initial_cash + init.perp_initial_wallet)
{
    validate_non_negative_int_(init.vip_level, "vip_level");
}

QTrading::Dto::Account::BalanceSnapshot Account::make_balance_(double wallet)
{
    QTrading::Dto::Account::BalanceSnapshot snapshot{};
    snapshot.WalletBalance = wallet;
    snapshot.MarginBalance = wallet;
    snapshot.AvailableBalance = wallet;
    snapshot.Equity = wallet;
    snapshot.OpenOrderInitialMargin = 0.0;
    snapshot.PositionInitialMargin = 0.0;
    snapshot.MaintenanceMargin = 0.0;
    return snapshot;
}

void Account::sync_total_cash_()
{
    total_cash_balance_ = spot_balance_.WalletBalance + perp_balance_.WalletBalance;
}

double Account::get_wallet_balance() const
{
    return perp_balance_.WalletBalance;
}

double Account::get_spot_cash_balance() const
{
    return spot_balance_.WalletBalance;
}

double Account::get_equity() const
{
    return total_cash_balance_;
}

uint64_t Account::get_state_version() const
{
    return state_version_;
}

double Account::total_unrealized_pnl() const
{
    return perp_balance_.UnrealizedPnl;
}

void Account::apply_spot_cash_delta(double delta)
{
    spot_balance_.WalletBalance += delta;
    spot_balance_.MarginBalance = spot_balance_.WalletBalance;
    spot_balance_.AvailableBalance = spot_balance_.WalletBalance - spot_balance_.PositionInitialMargin;
    spot_balance_.Equity = spot_balance_.WalletBalance;
    sync_total_cash_();
    ++state_version_;
}

void Account::apply_perp_wallet_delta(double delta)
{
    perp_balance_.WalletBalance += delta;
    perp_balance_.MarginBalance = perp_balance_.WalletBalance + perp_balance_.UnrealizedPnl;
    perp_balance_.AvailableBalance = perp_balance_.MarginBalance -
        perp_balance_.PositionInitialMargin -
        perp_balance_.OpenOrderInitialMargin;
    perp_balance_.Equity = perp_balance_.MarginBalance;
    if (perp_balance_.AvailableBalance < 0.0) {
        perp_balance_.AvailableBalance = 0.0;
    }
    perp_balance_.MaintenanceMargin = std::max(0.0, perp_balance_.MaintenanceMargin);
    sync_total_cash_();
    ++state_version_;
}

void Account::sync_open_order_initial_margins(double spot_open_order_initial_margin, double perp_open_order_initial_margin)
{
    spot_balance_.OpenOrderInitialMargin = std::max(0.0, spot_open_order_initial_margin);
    perp_balance_.OpenOrderInitialMargin = std::max(0.0, perp_open_order_initial_margin);

    spot_balance_.AvailableBalance =
        spot_balance_.WalletBalance -
        spot_balance_.PositionInitialMargin -
        spot_balance_.OpenOrderInitialMargin;
    if (spot_balance_.AvailableBalance < 0.0) {
        spot_balance_.AvailableBalance = 0.0;
    }

    perp_balance_.MarginBalance = perp_balance_.WalletBalance + perp_balance_.UnrealizedPnl;
    perp_balance_.Equity = perp_balance_.MarginBalance;
    perp_balance_.AvailableBalance =
        perp_balance_.MarginBalance -
        perp_balance_.PositionInitialMargin -
        perp_balance_.OpenOrderInitialMargin;
    if (perp_balance_.AvailableBalance < 0.0) {
        perp_balance_.AvailableBalance = 0.0;
    }
    ++state_version_;
}

void Account::update_perp_mark_state(double unrealized_pnl, double position_initial_margin, double maintenance_margin)
{
    perp_balance_.UnrealizedPnl = unrealized_pnl;
    perp_balance_.PositionInitialMargin = std::max(0.0, position_initial_margin);
    perp_balance_.MaintenanceMargin = std::max(0.0, maintenance_margin);
    perp_balance_.MarginBalance = perp_balance_.WalletBalance + perp_balance_.UnrealizedPnl;
    perp_balance_.Equity = perp_balance_.MarginBalance;
    perp_balance_.AvailableBalance = perp_balance_.MarginBalance -
        perp_balance_.PositionInitialMargin -
        perp_balance_.OpenOrderInitialMargin;
    if (perp_balance_.AvailableBalance < 0.0) {
        perp_balance_.AvailableBalance = 0.0;
    }
    ++state_version_;
}

bool Account::transfer_spot_to_perp(double amount)
{
    if (amount <= 0.0 || !can_debit_spot(amount)) {
        return false;
    }
    apply_spot_cash_delta(-amount);
    apply_perp_wallet_delta(amount);
    return true;
}

bool Account::transfer_perp_to_spot(double amount)
{
    if (amount <= 0.0 || !can_debit_perp(amount)) {
        return false;
    }
    apply_perp_wallet_delta(-amount);
    apply_spot_cash_delta(amount);
    return true;
}

Account::AccountInitConfig Account::build_init_from_balance_(double init_balance)
{
    AccountInitConfig init{};
    init.perp_initial_wallet = validate_non_negative_(init_balance, "init_balance");
    init.spot_initial_cash = 0.0;
    return init;
}

double Account::validate_non_negative_(double value, const char* field)
{
    if (value < 0.0) {
        throw std::runtime_error(std::string(field) + " must be >= 0");
    }
    return value;
}

void Account::validate_non_negative_int_(int value, const char* field)
{
    if (value < 0) {
        throw std::runtime_error(std::string(field) + " must be >= 0");
    }
}

} // namespace QTrading::Infra::Exchanges::BinanceSim
