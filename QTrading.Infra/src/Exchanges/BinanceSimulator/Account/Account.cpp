#include "Exchanges/BinanceSimulator/Account/Account.hpp"

#include "Exchanges/BinanceSimulator/Domain/AccountPolicyExecutionService.hpp"

#include <cmath>
#include <stdexcept>

namespace QTrading::Infra::Exchanges::BinanceSim {

Account::Account(double init_balance, int)
    : Account(build_init_from_balance_(init_balance)) {}

Account::Account(double init_balance, int vip_level, const AccountPolicies& policies)
    : Account(build_init_from_balance_(init_balance), policies)
{
    vip_level_ = vip_level;
}

Account::Account(const AccountInitConfig& init)
    : spot_balance_(make_balance_(validate_non_negative_(init.spot_initial_cash, "spot_initial_cash"))),
      perp_balance_(make_balance_(validate_non_negative_(init.perp_initial_wallet, "perp_initial_wallet"))),
      total_cash_balance_(init.spot_initial_cash + init.perp_initial_wallet),
      vip_level_(init.vip_level),
      policies_(AccountPolicies::Default())
{
    validate_non_negative_int_(init.vip_level, "vip_level");
}

Account::Account(const AccountInitConfig& init, const AccountPolicies& policies)
    : Account(init)
{
    policies_ = policies;
}

QTrading::Dto::Account::BalanceSnapshot Account::make_balance_(double wallet)
{
    QTrading::Dto::Account::BalanceSnapshot snapshot{};
    snapshot.WalletBalance = wallet;
    snapshot.MarginBalance = wallet;
    snapshot.AvailableBalance = wallet;
    snapshot.Equity = wallet;
    return snapshot;
}

void Account::sync_snapshot_(QTrading::Dto::Account::BalanceSnapshot& snapshot)
{
    snapshot.MarginBalance = snapshot.WalletBalance;
    snapshot.AvailableBalance = snapshot.WalletBalance;
    snapshot.Equity = snapshot.WalletBalance;
}

void Account::sync_total_cash_()
{
    total_cash_balance_ = spot_balance_.WalletBalance + perp_balance_.WalletBalance;
}

QTrading::Dto::Account::BalanceSnapshot Account::get_balance() const
{
    return get_perp_balance();
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
    return 0.0;
}

void Account::apply_spot_cash_delta(double delta)
{
    spot_balance_.WalletBalance += delta;
    sync_snapshot_(spot_balance_);
    sync_total_cash_();
    ++state_version_;
}

void Account::apply_perp_wallet_delta(double delta)
{
    perp_balance_.WalletBalance += delta;
    sync_snapshot_(perp_balance_);
    sync_total_cash_();
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

void Account::set_symbol_leverage(const std::string& symbol, double new_leverage)
{
    if (new_leverage <= 0.0 || !std::isfinite(new_leverage)) {
        return;
    }
    symbol_leverage_[symbol] = new_leverage;
}

bool Account::place_order(const std::string& symbol,
    double quantity,
    double price,
    QTrading::Dto::Trading::OrderSide side,
    QTrading::Dto::Trading::PositionSide position_side,
    bool reduce_only,
    const std::string& client_order_id,
    SelfTradePreventionMode stp_mode)
{
    (void)client_order_id;
    (void)stp_mode;

    const bool queued = Domain::AccountPolicyExecutionService::TryQueueOrder(
        symbol,
        quantity,
        price,
        side,
        position_side,
        reduce_only,
        next_order_id_,
        open_orders_);
    if (queued) {
        ++state_version_;
    }
    return queued;
}

void Account::update_positions(const std::unordered_map<std::string, QTrading::Dto::Market::Binance::TradeKlineDto>& symbol_kline)
{
    const std::unordered_map<std::string, double> empty_mark_price{};
    update_positions(symbol_kline, empty_mark_price);
}

void Account::update_positions(const std::unordered_map<std::string, QTrading::Dto::Market::Binance::TradeKlineDto>& symbol_kline,
    const std::unordered_map<std::string, double>& symbol_mark_price)
{
    const Domain::AccountPolicyUpdateResult result = Domain::AccountPolicyExecutionService::ApplyUpdates(
        symbol_kline,
        symbol_mark_price,
        policies_,
        vip_level_,
        symbol_leverage_,
        open_orders_,
        positions_);
    if (result.perp_wallet_delta != 0.0) {
        apply_perp_wallet_delta(result.perp_wallet_delta);
    }
    if (result.filled_count > 0) {
        ++state_version_;
    }
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

AccountPolicies AccountPolicies::Default()
{
    AccountPolicies policies{};
    policies.fee_rates = [](int) {
        return std::make_tuple(0.0002, 0.0004);
    };
    return policies;
}

} // namespace QTrading::Infra::Exchanges::BinanceSim
