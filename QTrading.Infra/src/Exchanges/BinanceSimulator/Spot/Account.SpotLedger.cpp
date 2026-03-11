#include "Exchanges/BinanceSimulator/Account/Account.hpp"

#include <algorithm>
#include <cmath>

using QTrading::Dto::Trading::InstrumentType;

double Account::spot_open_buy_market_notional_total_() const
{
    double notional = 0.0;
    const double slippage_mul = 1.0 + std::max(0.0, market_slippage_buffer_);
    for (const auto& kv : spot_open_buy_market_qty_by_symbol_) {
        const double qty = kv.second;
        if (qty <= 1e-12) {
            continue;
        }
        const double trade = get_last_trade_price_(kv.first);
        if (trade <= 0.0) {
            continue;
        }
        notional += qty * trade * slippage_mul;
    }
    return notional;
}

double Account::spot_open_buy_reserved_cash_() const
{
    const double buy_open_notional = std::max(0.0, spot_open_buy_limit_notional_total_) +
        spot_open_buy_market_notional_total_();
    if (buy_open_notional <= 0.0) {
        return 0.0;
    }

    if (spot_commission_mode_ != SpotCommissionMode::QuoteAsset) {
        return buy_open_notional;
    }

    const auto fee_rates = get_fee_rates(InstrumentType::Spot);
    const double worst_fee_rate = std::max(0.0, std::max(std::get<0>(fee_rates), std::get<1>(fee_rates)));
    return buy_open_notional * (1.0 + worst_fee_rate);
}

QTrading::Dto::Account::BalanceSnapshot Account::get_spot_balance() const
{
    QTrading::Dto::Account::BalanceSnapshot s;
    const double wallet = spot_ledger_.cash_balance();
    s.WalletBalance = wallet;
    const double reserved = spot_open_buy_reserved_cash_();

    s.OpenOrderInitialMargin = reserved;
    s.AvailableBalance = std::max(0.0, wallet - reserved);
    s.MarginBalance = wallet;
    s.Equity = wallet;
    return s;
}

double Account::get_spot_cash_balance() const
{
    return spot_ledger_.cash_balance();
}

bool Account::transfer_spot_to_perp(double amount)
{
    if (!std::isfinite(amount) || amount <= 0.0) {
        return false;
    }
    const auto spot = get_spot_balance();
    if (spot.AvailableBalance + 1e-12 < amount) {
        return false;
    }
    spot_ledger_.debit_cash(amount);
    perp_ledger_.credit_wallet(amount);
    mark_balance_dirty_();
    ++state_version_;
    return true;
}

bool Account::transfer_perp_to_spot(double amount)
{
    if (!std::isfinite(amount) || amount <= 0.0) {
        return false;
    }
    const auto perp = get_perp_balance();
    if (perp.AvailableBalance + 1e-12 < amount) {
        return false;
    }
    perp_ledger_.debit_wallet(amount);
    spot_ledger_.credit_cash(amount);
    mark_balance_dirty_();
    ++state_version_;
    return true;
}
