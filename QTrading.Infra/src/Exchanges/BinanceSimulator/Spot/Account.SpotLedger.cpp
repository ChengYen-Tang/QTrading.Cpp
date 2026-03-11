#include "Exchanges/BinanceSimulator/Account/Account.hpp"

#include <algorithm>
#include <cmath>

using QTrading::Dto::Trading::InstrumentType;
using QTrading::Dto::Trading::OrderSide;

QTrading::Dto::Account::BalanceSnapshot Account::get_spot_balance() const
{
    QTrading::Dto::Account::BalanceSnapshot s;
    s.WalletBalance = spot_ledger_.cash_balance();
    const auto fee_rates = get_fee_rates(InstrumentType::Spot);
    const double worst_fee_rate = std::max(0.0, std::max(std::get<0>(fee_rates), std::get<1>(fee_rates)));

    double reserved = 0.0;
    const bool quote_fee_mode = (spot_commission_mode_ == SpotCommissionMode::QuoteAsset);
    for (const auto& ord : open_orders_) {
        if (ord.instrument_type != InstrumentType::Spot) {
            continue;
        }
        if (ord.side != OrderSide::Buy || ord.closing_position_id >= 0 || ord.reduce_only) {
            continue;
        }
        if (ord.quantity <= 0.0) {
            continue;
        }

        if (ord.price > 0.0) {
            const double notional = ord.quantity * ord.price;
            reserved += quote_fee_mode ? (notional * (1.0 + worst_fee_rate)) : notional;
            continue;
        }

        const double trade = get_last_trade_price_(ord.symbol);
        if (trade <= 0.0) {
            continue;
        }
        const double notional = ord.quantity * trade * (1.0 + std::max(0.0, market_slippage_buffer_));
        reserved += quote_fee_mode ? (notional * (1.0 + worst_fee_rate)) : notional;
    }

    s.OpenOrderInitialMargin = reserved;
    s.AvailableBalance = std::max(0.0, spot_ledger_.cash_balance() - reserved);
    s.MarginBalance = spot_ledger_.cash_balance();
    s.Equity = spot_ledger_.cash_balance();
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
